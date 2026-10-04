---
type: design
status: current
summary: How STM32U375 tags sleep -- Stop 3 as the terminal sleep, Stop 2 or Stop 1 between IMU wakes, Sleep as the idle default, scoped STOP around SPI transfers -- the monitor guards, the layout sensitivity, and what to measure after a change.
last-verified: a87fc84a
---

# STM32U375 Low Power

On the STM32U375 the **terminal sleep is Stop 3**, not Standby: `godown(STANDBY)`
enters Stop 3 and the RTC wake becomes a reset that the boot path classifies as
a Standby wake. **Between IMU wakes during RUNNING the tag sleeps in Stop 2**
on IMUTagNandBmp581 (Stop 1 on IMUTagNand). Outside those scopes the idle
thread uses plain **Sleep**. On this part, whether a requested low-power mode
is actually reached depends on the layout of the image, so every image change
needs a current measurement.

Code: `common/core/src/pwr-u375.c` (included by `pwr.c` on U3 builds),
`common/core/src/main.c`, the IMUTag `families/IMUTag/src/state_run.c`, and each
U375 target's `src/power_modes.c`. The STM32L432 targets use
`pwr-l432.c` and true Standby or Shutdown; nothing here applies to them.

## The three low-power paths

| Path | When | Mode | Entered by |
| --- | --- | --- | --- |
| Terminal sleep | A state handler returns `STANDBY` (idle, configured, hibernating, finished, aborted) | Stop 3, then reset on wake | `godown()` -> `tagPowerEnterTerminalSleep()` -> `tagPowerEnterStop3()` |
| Run sleep | `TagState_RUNNING`, main thread waiting for hardware events | The mode `Running()` returns: Stop 2 or Stop 1 | ChibiOS idle hook -> `tagPowerEnterIdleMode()` |
| Returned idle | Everything else, and scoped waits | Sleep by default; STOP0 or STOP1 inside SPI brackets | ChibiOS idle hook -> `tagPowerEnterIdleMode()` |

`enum Sleep` carries both kinds of request. `godown()` acts only on `STANDBY`;
`SLEEP`, `STOP0`, `STOP1` and `STOP2` are returned-idle selectors consumed by
the idle hook. `SHUTDOWN` exists in the enum for the L432 but is not used on
U3: `godown(SHUTDOWN)` returns without sleeping, and the idle hook demotes
`SHUTDOWN` and `STANDBY` to `SLEEP` (`tagPowerReturnedIdleMode()`). The idle
selector is passed through the shared `volatile enum Sleep idlePowerMode`
(declared in `core_runtime.h`, defined in `state_machine.c`).

## Terminal sleep: Stop 3

`tagPowerEnterStop3()` returns without sleeping unless the request is
`STANDBY` and `monitorIsAttached()` is false. Otherwise, in order:

1. `tagDevicesApplyPowerState(TAG_DEVICE_POWER_STANDBY_ENTRY, state)` quiesces
   the devices. On IMUTag this is also where the I2C bus is cleared (see
   [I2C Bus Recovery](i2c-bus-recovery.md)).
2. `tagDevicesConfigureWakeupSources(state, isActive)` arms the wake inputs.
   If it returns false the function returns, and the state machine runs again.
3. `tagPowerConfigureStop3RtcWake()` routes the RTC to wake line 7:
   `PWR_WUCR3.WUSEL7 = 11` (RTC_ALRA/ALRB/WUT/TS), polarity in `PWR_WUCR2`,
   `PWR_WUCR1.WUPEN7`. Each can be overridden with the
   `TAG_STM32U3_STOP3_RTC_WUCR*` macros.
4. The Standby pull masks are made effective by setting `PWR_APCR.APC`. Both
   U375 targets set `TAG_STANDBY_PULLS_CONFIGURED_BY_MCUCONF`, so the masks
   generated from the board's `board-customizations.json` are loaded into
   `PWR_PUCRx`/`PWR_PDCRx` from `mcuconf.h` at HAL startup. Without that
   macro, a board whose generated header sets `BOARD_STANDBY_HAS_CONFIG` has
   `tagApplyBoardStandbyPins()` called here instead, and any other board has
   `tagDevicesApplyStandbyPins()` called here. The bias policy per pin is in
   [IMUTagNandv1](../../../../boards/IMUTagNandv1/standby-pins.md) and
   [IMUTagNandv2](../../../../boards/IMUTagNandv2/standby-pins.md).
5. `tagDevicesDisableWakeupSources()` runs. On IMUTag it clears wake pin 1,
   the IMU interrupt, so the IMU cannot wake the terminal sleep. Then the PWR
   wake flags are cleared, and `tagPowerClearFlashErrorFlags()` clears the `FLASH_SR` error flags and the
   `FLASH_ECCR` ECC flags.
6. `DBGMCU_CR` is cleared, unless the image was built with
   `TAG_DEBUG_LOW_POWER`, which must never ship (see
   [Debugging a Tag](../../../../../docs/bench/debugging-a-tag.md)).
7. `PWR_CR1.LPMS = 011` (Stop 3), `SLEEPDEEP`, `DSB`, `ISB`, `WFI`.

On wake, `tagPowerResetAfterStop3Wake()` writes
`pState->synthetic_standby_wake = TAG_SYNTHETIC_STANDBY_WAKE_MAGIC` and
`pState->resetCause = resetStandby`, then calls `NVIC_SystemReset()`. Early boot
in `main.c` consumes the marker: with the marker present and `RCC_CSR.SFTRSTF`
set, the reset is classified `resetStandby`. The boot after Stop 3 is therefore
the same boot that followed a Standby wake. Nothing resumes in place, and no
clock tree is restored after Stop 3.

At rest Stop 3 costs about 3.6 uA more than a Standby that works. The
measurements are in
[0008. STM32U375 terminal sleep is Stop 3](../../../../../docs/decisions/0008-u375-terminal-sleep-is-stop-3.md).

**Why not Standby.** On this part a Standby request (`LPMS = 1xx`) is declined
depending on image layout. The firmware reaches the `WFI` with every documented
precondition met, and the core drops into ordinary Sleep with its bus clocks
running, at about 1 mA. Stop 3, with the same preparation, entered at every
layout that stalls Standby. The cause was never found. Every mechanism tested
and excluded is recorded in
[the Standby investigation](investigations/2026-09-u375-standby-layout-dependence.md);
read it before proposing another. `tagPowerEnterStandby()` remains in
`pwr-u375.c`, marked `__attribute__((unused))` and `noinline`, as the reference
for that fault.

## Run sleep: Stop 2 or Stop 1

The IMUTag `Running()` returns `IMUTAG_RUN_SLEEP_MODE` as the sleep mode for the
RUNNING wait. A target selects it in its `inc/custom.h`:

| `IMUTAG_RUN_SLEEP_STOP2` | Run sleep | Targets |
| --- | --- | --- |
| `1` | `STOP2` | IMUTagNandBmp581 |
| `0` (family default) | `STOP1` when `USE_STOP1`, else `SLEEP` | IMUTagNand |

`IMUTAG_RUN_SLEEP_MODE` is derived from that switch; defining it in a target
is a compile error. Stop 2 keeps LPTIM1 clocked but not LPTIM2, so
`IMUTAG_RUN_SLEEP_STOP2` is legal only with the IMU trigger on LPTIM1, and
`families/IMUTag/src/devices.c` rejects the other combination at compile time.
IMUTagNandBmp581 keeps `USE_STOP1 = 1` because `stopMilliseconds()` and the
LPTIM1 trigger gate still read it.

Stop 2 is used because Stop 1 run current depends on layout. Stop 2 is stable
across layouts and is lower at every sample rate. See
[0007. STM32U375 run-mode sleep is Stop 2](../../../../../docs/decisions/0007-u375-run-sleep-is-stop-2.md).

The main loop in `main.c` applies the mode only in RUNNING:

```c
idlePowerMode = sleepmode;
eventmask_t wait_events = EVT_HARDWARE_ALL | MON_WORK_ALL;
if (isMonitorEnabled())
  wait_events |= EVT_MONITOR_ALL;
pending_events = chEvtWaitAny(wait_events);
idlePowerMode = TAG_DEFAULT_IDLE_POWER_MODE;
```

`MON_WORK_ALL` is in the mask so that a posted stop or other monitor work
wakes the RUNNING wait by itself. In any other state the loop never blocks:
it collects pending events with `chEvtGetAndClearEvents(EVT_ALL_DEFINED)` and
re-runs the state machine. A blocking wait there could miss a `MONITORSTOP`
that arrives between the monitor test and the wait. The wake model is
event-driven: the IMU FIFO watermark and other hardware events wake the main
thread.

## Returned idle

`TAG_DEFAULT_IDLE_POWER_MODE` is `SLEEP`, the mode the idle thread uses
whenever no scope has set a deeper one. See
[0003. STM32U375 returned-idle default is Sleep](../../../../../docs/decisions/0003-u375-returned-idle-default-is-sleep.md).

Each U375 target's `chconf.h` wires the ChibiOS idle hooks to `idle_enter()`,
`idle_loop()` and `idle_leave()` in its `src/power_modes.c`. `idle_enter()` and
`idle_leave()` are empty. `idle_loop()`:

- returns without `WFI` while `isMonitorEnabled()`, so the debug interface
  and the monitor stay serviceable;
- uses `SLEEP` while a ChibiOS system-timer alarm is active
  (`stIsAlarmActive()`), because the system timer is TIM2 and does not run in
  Stop, so any pending timeout must be able to wake the core;
- otherwise calls `tagPowerEnterIdleMode(idlePowerMode)`.

`tagPowerEnterIdleMode()` repeats the monitor check. It turns `STANDBY` and
`SHUTDOWN`, which belong to `godown()`, into `SLEEP`. For `SLEEP` it executes
`WFI` with `SLEEPDEEP` clear. For `STOP0`, `STOP1` and `STOP2` it applies the
`DBGMCU` setting, writes `PWR_CR1.LPMS` (0, `LPMS_0` and `LPMS_1`
respectively), sets `SLEEPDEEP`, executes `WFI`, and clears `SLEEPDEEP` on
return. No VCORE range is restored on wake. `TAG_IDLE_STOP_DIAGNOSTICS` pulses
`LINE_LED1` around STOP entry. Leave it off for current measurements.

### Scoped STOP around SPI transfers

Two driver-level brackets save `idlePowerMode`, set a deeper mode for one
blocking transfer, and restore it:

- `tagStorageSpiBlockWrite()` (`common/storage/inc/storage_spi.h`) uses
  `STOP1` around the bulk payload of an external flash write, such as MX25U
  page-program data or GD5F program-cache data. Command, address, write-enable
  and status-poll transactions, and anything no longer than
  `TAG_SPI_POLLED_TRANSFER_MAX`, stay on the polled path.
- `tagSpiRead()` in the ChibiOS SPI backend (`spi_bus_chibios.inc`) uses
  `STOP0` around `spiReceive()`.

Small register transactions stay on the polled path and do not change
`idlePowerMode`. Do not make a deeper mode the default for all SPI I/O. The
policy for new storage variants is in the
[storage README](../../storage/README.md).

### Internal flash writes and VCORE

STM32U3 internal flash row programming needs VCORE Range 1.
`FLASH_Program_Row()` switches to Range 1 when needed, programs, and restores
the configured run range when that range is Range 2. The change stays inside
the flash writer, and the idle hook does not raise VCORE on wake. Bench
measurement on IMUTagNand put the Range 2 to Range 1 step at about 0.8 uJ per
checkpoint write. That is the cost of using STOP modes before a fixed-cadence
internal checkpoint, and is consistent with charging the VCORE capacitance
(0.5 x 4.7 uF x (1.2^2 - 1.0^2) is about 1.0 uJ).

## Monitor guards

The two paths use different predicates:

| Path | Guard |
| --- | --- |
| Terminal sleep (`tagPowerEnterStop3()`) | `monitorIsAttached()` |
| Returned idle (`idle_loop()`, `tagPowerEnterIdleMode()`) and the RUNNING wait mask | `isMonitorEnabled()` |

On U3, `isMonitorEnabled()` is `monitorIsAttached() || monitorAttachGraceActive()`,
and `MONCONNECTED` is deliberately not consulted. The definitions and
the reasons are in [Tag Monitor Interface](../../../../../docs/shared/monitor-interface.md#attachment-state)
and [0006](../../../../../docs/decisions/0006-monitor-u3-attachment-is-the-shared-session.md).
The difference between the two paths means that during an attach grace period, Stop 3 can
be entered while returned-idle STOP cannot. Whether that is intended is an open
question in [the tag TODO](../../../TODO.md). Debugger attachment alone is not
monitor attachment: `monitorIsAttached()` can be false under a debugger.

## What is not provided

- No LPTIM-backed ChibiOS system timer: ChibiOS time does not advance in STOP,
  which is why the idle hook falls back to Sleep while a system-timer alarm is
  pending. The proposal is [LPTIM System Timer](lptim-system-timer.md).
- No general peripheral parking for returned STOP; owning drivers open and
  close their bus sessions normally.
- No autonomous SPI, I2C or DMA operation through STOP as a shared capability.
- No SRAM1 power-down.

## Layout sensitivity

Low-power entry on this part depends on where code lands in the image, not
only on what the code does:

- A Standby request was declined at some layouts: idle current flipped
  between about 5 uA and about 1040 uA with `nop` padding in an unrelated function.
- Stop 1 current during a run moved by about 200 uA between builds that
  differed only in layout. The table is in
  [0007](../../../../../docs/decisions/0007-u375-run-sleep-is-stop-2.md).

Stop 3 and Stop 2 have survived every layout tried. They have not been proven
immune. So:

- **Any change to the image can expose this class of fault.** When a change
  that cannot alter behaviour moves idle or run current, suspect layout before
  logic.
- **Do not instrument the arming window.** A probe placed between the `LPMS` and
  `SLEEPDEEP` writes and the `WFI` moves the layout, so a passing experiment
  proves nothing about the shipped image. Instrument at boot through the
  retained scratchpad instead.

## After a change: measure

Anything that touches `main.c` boot cleanup, `state_machine.c`, `pwr.c`,
`pwr-u375.c`, `godown()`, `pState`, or device power sequencing needs a current
measurement, not an argument. A clean build and passing functional tests prove
nothing about sleep. Measure every resting state with the clock set, and
measure the run. The procedure and tools are in
[Power Testing](../../../../../docs/bench/power-testing.md); the life-cycle check
and `tag_release_check.py` are described in its section 6.

When the number is wrong, rule out the cheap explanations before reaching for
a mechanism:

- **Pin state.** A GPIO left driven against the board's 4.7 kOhm I2C pull-ups
  sinks about 700 uA per line while the MCU really is asleep. A tag that
  reports IDLE at run current may not be stuck in `WFI` at all. Ask what the
  last code to touch those pins left them as. An I2C bus clear at the wrong
  call site does exactly this: see [I2C Bus Recovery](i2c-bus-recovery.md#where-a-clear-must-not-go).
- **Bisect against recent commits** before theorising, and bisect against the
  last known-good image rather than reasoning about the fault.
- **Trust only repeated measurements:** three or four trials per point
  ([Power Testing](../../../../../docs/bench/power-testing.md#4-taking-the-measurement)).

The flash error-flag clear is not a cause of idle current.
`tagPowerClearFlashErrorFlags()` runs before every terminal sleep, and the
flags have been captured clean in a build that failed to sleep. See
[the Standby investigation](investigations/2026-09-u375-standby-layout-dependence.md)
and [the latched-flag investigation](investigations/2026-09-u3-latched-flash-error-flags.md).
