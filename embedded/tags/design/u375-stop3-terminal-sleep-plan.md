# STM32U375 Stop3 Terminal Sleep Plan

This note records the STM32U375 terminal Stop3 replacement for Standby on
IMUTagNand-class tags. The runtime idle Stop-mode implementation is documented
separately in `u375-stop-support.md`.

## Goal

Use Stop3 as the terminal low-power state for STM32U375 tags that cannot use
the existing STM32L432 Standby pin model reliably, while leaving the STM32L432
Standby path unchanged.

The target behavior is:

- STM32L432 targets continue to treat `godown(STANDBY)` as true MCU Standby.
- STM32U375 targets treat the same semantic sleep request as terminal Stop3.
- RTC alarm or wakeup events can bring the U375 core back from Stop3.
- The run clock tree is restored immediately after Stop3 wake before normal
  drivers or the state machine run again.
- IMUTagNand does not apply generated Standby pull-up or pull-down masks for
  the U375 Stop3 terminal path.

## Current Shape

The common state machine already returns `STANDBY` for idle, configured,
hibernating, finished, and aborted quiet states. Keep that semantic request.
Do not rename the state-machine return values merely because the U375 backend
will implement the request with Stop3.

Before the U3 split, `godown()` in `embedded/tags/common/core/src/pwr.c` had
one shared implementation. It prepared devices for Standby, applied board or
device Standby pull configuration, configured wakeup sources, selected Standby
in `PWR->CR1.LPMS`, set `SLEEPDEEP`, and executed `WFI`.

IMUTagNand already has separate idle hooks in
`embedded/tags/IMUTagNand/src/power_modes.c`, but those are runtime idle hooks
for `STOP0`, `STOP1`, and `STOP2`. The terminal Stop3 path should live with
terminal sleep entry, not in the idle hook.

The STM32U3 RTCv3 driver enables RTC EXTI and RTC interrupt vectors during
`rtc_lld_init()`. The likely missing piece for Stop3 is not ordinary RTC alarm
programming; it is the U3 Stop3 return sequence and any U3-specific PWR
wake-source selection required for RTC-originated wake.

## Implemented Split

Keep `godown(enum Sleep sleepmode)` as the public entry point, but split its
implementation internally:

- `tagPowerEnterStandby()` for existing STM32L432-style Standby.
- `tagPowerEnterStop3()` for STM32U375 terminal Stop3.

The L4 function should preserve the current sequence, including generated
`board_standby.h` masks, `PWR` Standby pull activation, wakeup-pin setup, and
the monitor guard. This path is known firmware behavior and should not be
reworked as part of the U3 Stop3 change.

The U3 function:

1. Return immediately unless `sleepmode == STANDBY` and the monitor is not
   attached.
2. Quiesce devices using the terminal/standby device lifecycle hook, but avoid
   applying MCU Standby pull masks.
3. Ensure the selected RTC alarm or wakeup source is already armed through the
   existing `rtcSetAlarm()` or `rtcSTM32SetPeriodicWakeup()` path.
4. Ensure the RTC interrupt path remains enabled through the RTCv3 driver.
5. Select the U3 RTC Stop3 wake input using `PWR->WUCR1`, `PWR->WUCR2`, and
   `PWR->WUCR3`. The initial default is wake line 7, source select 3, high
   polarity; board or target code can override those masks with
   `TAG_STM32U3_STOP3_RTC_WUCR*` defines if later measurements require it.
6. Select Stop3 in `PWR->CR1.LPMS`.
7. Set `SCB->SCR.SLEEPDEEP`, execute `DSB/WFI/ISB`, and then restore clocks
   immediately after wake.

The U3 implementation treats a return from `WFI` as a real terminal-sleep
wake. After clocks are restored, `godown()` posts `EVT_WAKE_STANDBY` directly
to the main thread. This gives configured-state delayed starts a deterministic
state-machine pass after each Stop3 wake without depending on the RTC interrupt
callback to have posted a specific alarm bit.

The main loop should only enter the runtime idle `chEvtWaitAny()` path while
`pState->state == TagState_RUNNING`. Terminal states such as `IDLE` and
`CONFIGURED` should return from `godown()`, collect pending events, and
immediately re-evaluate the state machine. Otherwise a configured delayed start
can wake from Stop3 at a minute boundary and then sit in a runtime WFI/Stop1
wait until another external event, such as monitor attach, disturbs it.

Monitor-reset recovery also has a U3-relevant CONFIGURED case: if a monitor
reattaches while a delayed start is sleeping in Stop3, the retained
`TagState_CONFIGURED` state is continued instead of being treated as a field
power failure and converted to ABORTED.

The U3 monitor detach path must also signal the main thread after publishing
detached state. Without that signal, a delayed-start command can return from
`godown()` while the monitor is still attached, enter the runtime idle
`chEvtWaitAny()` path, and remain in Stop1 until the next RTC minute event.
Posting a monitor event on explicit `MONITORSTOP` gives the main loop an
immediate pass to observe `monitorIsAttached() == false` and enter terminal
Stop3.

## Clock Restore Contract

Clock restoration is the central Stop3 wake requirement.

Do not call `stm32_clock_init()` after Stop3. That startup helper resets
peripheral buses, redoes backup-domain setup, and is too broad for a Stop3
resume path.

The preferred first restore primitive is:

```c
(void)hal_lld_clock_switch_mode(&hal_clkcfg_default);
```

This uses the ChibiOS STM32U3 clock configuration object and restores the
normal oscillator, voltage-scale, prescaler, flash-latency, and
`SystemCoreClock` state without doing the full startup reset sequence.

The Stop3 return order should be:

```c
__DSB();
__WFI();
__ISB();

(void)hal_lld_clock_switch_mode(&hal_clkcfg_default);
CLEAR_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);
```

If hardware testing shows that Stop3 also loses or perturbs static peripheral
kernel clock selections, add a small U3-only helper that reapplies the relevant
`mcuconf.h` clock-source constants without resetting peripherals. For
IMUTagNand the critical selections are:

- `STM32_SW = RCC_CFGR1_SW_MSIS`
- `STM32_STOPWUCK = RCC_CFGR1_STOPWUCK_MSIS`
- `STM32_STOPKERWUCK = RCC_CFGR1_STOPKERWUCK_MSIK`
- `STM32_SPI1SEL = RCC_CCIPR1_SPI1SEL_MSIK`
- `STM32_LPTIM2SEL = RCC_CCIPR1_LPTIM2SEL_LSE`
- `STM32_RTCSEL = RCC_BDCR_RTCSEL_LSE`

Treat this as targeted repair after measurement, not as permission to rerun the
whole clock startup path.

## Wake Flags

Do not make `PWR->WUSCR` clearing the main theory of the fix.

Clearing wake flags can be useful when stale flags prevent a later Stop3 entry,
or when diagnostics need a clean before/after view of the wake source. It is
not currently the most likely reason the RTC wake does not produce a usable
resume. The implementation therefore leaves U3 wake-flag clearing disabled by
default and exposes `TAG_STM32U3_STOP3_CLEAR_WAKE_FLAGS` as an explicit
bring-up switch. The bring-up focus should be:

- RTC event armed;
- RTC interrupt/EXTI path enabled;
- U3 Stop3 wake-source selection correct;
- clocks restored immediately after wake.

If repeated Stop3 entries fail only after the first successful wake, then make
`WUSCR` handling part of that specific re-entry fix.

## Validation Plan

1. Build IMUTagNand and at least one STM32L432 target such as PresTag.
2. On IMUTagNand, arm a short RTC alarm and enter the U3 terminal Stop3 path
   with sensors inactive.
3. Verify current draw remains near the measured Stop3 floor.
4. Verify the RTC wake returns from `WFI`, restores clocks, and reaches the main
   event loop.
5. Verify a configured-start wait can wake on RTC and transition to RUNNING.
6. Verify normal monitor attach still prevents terminal sleep.
7. Re-test STM32L432 Standby wake and monitor attach to confirm the L4 path was
   preserved.

## Open Questions

- Is wake line 7/source select 3/high polarity the correct RTC Stop3 wake
  selection on the final STM32U375 reference-manual reading and on the
  IMUTagNand board?
- Does Stop3 preserve all needed `CCIPR` peripheral kernel selections on
  STM32U375, or does IMUTagNand need a narrow post-wake static-clock repair?
- Is `EVT_WAKE_STANDBY` sufficient for all U3 terminal Stop3 returns, or should
  the implementation also surface the specific RTC alarm bit for diagnostics?
