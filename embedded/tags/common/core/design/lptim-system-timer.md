# LPTIM System Timer Design

## Purpose

This document proposes a tag-core system timer option that uses STM32 LPTIM3 or
LPTIM4 as the ChibiOS ST low-level driver. The goal is to let the ChibiOS
system timer continue running from the 32.768 kHz low-speed clock domain while
the MCU enters Stop modes, without consuming the STM32 RTC peripheral or the
existing LPTIM1 one-shot delay path.

The design is intended for STM32U3 tag targets first. It should live under
`embedded/tags/common/core` as project-owned integration code rather than as an
edit to the `ChibiOS/` submodule.

## Background

ChibiOS exposes timing through the ST driver. STM32 targets currently include
`ChibiOS/os/hal/ports/STM32/LLD/SYSTICKv1`, which can use a general-purpose
timer in free-running mode or Cortex SysTick in periodic mode. ChibiOS also has
`SYSTICKv2`, an RTC-based free-running ST driver for selected STM32 families.

The tag firmware mostly configures `CH_CFG_ST_RESOLUTION = 32`,
`CH_CFG_ST_FREQUENCY = 10000`, and `STM32_ST_USE_TIMER = 2`. That gives a
convenient 10 kHz tick from TIM2 while the core clock is running, but it does
not keep time through low-power Stop modes. LPTIM peripherals can keep running
from LSE or LSI in Stop mode, but their simple prescalers cannot synthesize
arbitrary round frequencies such as 10 kHz from 32.768 kHz.

The active common runtime already uses LPTIM1 for explicit low-power delays in
`time.c`, so the system timer should use LPTIM3 or LPTIM4 to avoid resource
conflicts.

## Requirements

- Provide a ChibiOS-compatible `hal_st_lld.h` / `hal_st_lld.c` pair.
- Support only free-running mode.
- Use one alarm channel for the OS timer queue.
- Use a 16-bit hardware counter.
- Support LPTIM3 or LPTIM4 selection at build time.
- Run from the existing 32.768 kHz low-speed source when available.
- Preserve the STM32 RTC peripheral for calendar/RTC-chip integration.
- Keep the implementation local to project-owned source under common core.
- Make the feature opt-in per target.

The driver shape is:

```c
#define HAL_ST_USE_TIMER_WIDTH 16
#define ST_LLD_NUM_ALARMS 1
#define ST_LLD_HAS_PERIODIC_IRQ FALSE

#ifdef __cplusplus
extern "C" {
#endif
void st_lld_init(void);
bool st_lld_is_alarm_active(void);
systime_t st_lld_get_counter(void);
void st_lld_start_alarm(systime_t time);
void st_lld_stop_alarm(void);
#ifdef __cplusplus
}
#endif
```

The final header should also provide any ChibiOS-required helpers used by the
current HAL ST API, such as `st_lld_set_alarm()` and `st_lld_get_alarm()`, even
if they are implemented as wrappers around the primitive operations above.

## Proposed Implementation

Add a local driver directory:

```text
embedded/tags/common/core/stv3/
  driver.mk
  hal_st_lld.c
  hal_st_lld.h
```

The tag build should enable this driver with an explicit option:

```make
USE_LPTIM_ST = yes
```

When enabled, the tag make layer should remove the STM32 `SYSTICKv1` source and
include directory contributed by `platform.mk`, then add the local STv3 source
and include directory. This keeps `ChibiOS/` untouched and makes the driver
selection reversible per target.

Recommended target configuration for a first STM32U3 target:

```c
#define CH_CFG_ST_RESOLUTION                16
#define CH_CFG_ST_FREQUENCY                 1024
#define CH_CFG_ST_TIMEDELTA                 3

#define STM32_ST_USE_LPTIM                  3
#define STM32_ST_IRQ_PRIORITY               8
#define STM32_LPTIM34SEL                    RCC_CCIPR3_LPTIM34SEL_LSE
```

`1024 Hz` is the recommended starting point because it divides 32.768 kHz
exactly with a `/32` prescaler. It avoids fractional timekeeping and leaves a
16-bit wrap interval of 64 seconds.

## Driver Behavior

`st_lld_init()` should:

- validate `OSAL_ST_MODE == OSAL_ST_MODE_FREERUNNING`;
- validate `OSAL_ST_RESOLUTION == 16`;
- validate that the selected LPTIM clock divides exactly to
  `OSAL_ST_FREQUENCY`;
- enable the selected LPTIM APB3 clock;
- enable the selected LPTIM Stop-mode clock;
- optionally freeze the selected LPTIM during debug;
- configure the LPTIM prescaler and continuous-counting mode;
- set `ARR = 0xFFFF`;
- clear pending interrupt flags;
- enable the selected LPTIM NVIC vector.

`st_lld_get_counter()` should return the 16-bit LPTIM counter. If the STM32U3
counter read can be asynchronous to the APB domain, use the stable-read pattern
recommended by the reference manual rather than a single raw read.

`st_lld_start_alarm()` and `st_lld_set_alarm()` should:

- write the compare register for channel 1;
- wait for the compare-update acknowledgement flag;
- clear stale compare-match flags;
- enable the compare-match interrupt.

`st_lld_stop_alarm()` should disable the compare interrupt and clear pending
compare-match flags.

The ISR should clear the LPTIM compare flag and call:

```c
osalSysLockFromISR();
osalOsTimerHandlerI();
osalSysUnlockFromISR();
```

Only one ST alarm is required. The design does not attempt to expose extra
LPTIM compare channels as ChibiOS multi-alarm callbacks.

## Alarm Latency

LPTIM compare programming is not instantaneous when the timer is clocked from a
slow asynchronous source. Setting an alarm incurs a delay measured in low-speed
timer source ticks. With the current RV3028-derived 32.768 kHz source, this is
small in wall-clock time but meaningful relative to near-deadline alarms.

The driver should make this explicit by defining a guard constant, for example:

```c
#define STM32_ST_LPTIM_WRITE_GUARD_TICKS 3U
```

The ChibiOS configuration should then set `CH_CFG_ST_TIMEDELTA` large enough to
cover the compare-write delay plus ISR and register synchronization margin. The
initial recommendation is `3` ticks at a 1024 Hz ST frequency, then adjust after
hardware measurement.

The implementation should test alarms scheduled at `now + CH_CFG_ST_TIMEDELTA`
and across the 16-bit wrap boundary.

## Integration Cleanup

Some current common and family code assumes the ChibiOS ST driver is TIM2 and
touches `STM32_ST_TIM->PSC` directly to scale monitor/debug timing. That code
cannot be used when ST is backed by LPTIM.

Before enabling STv3 on a target:

- audit direct uses of `STM32_ST_TIM`;
- either compile those paths out for `USE_LPTIM_ST=yes` or replace them with a
  neutral hook;
- verify that LPTIM3 or LPTIM4 is not already owned by the selected tag family;
- keep LPTIM1 reserved for the existing explicit low-power delay path unless
  that path is redesigned at the same time.

## Alternative: Sleep While ST Alarm Is Active

An alternative is to leave the existing TIM2-backed ST driver in place and
change the low-power entry policy: whenever the ChibiOS ST alarm is active,
enter ARM Sleep rather than Stop0/Stop1/Stop2/Stop3. In Sleep mode the core
stops executing, but the normal timer clock domain remains active, so TIM2 can
wake the CPU through the existing ST interrupt.

The policy would look like:

```c
if (stIsAlarmActive()) {
  enter_sleep();
} else {
  enter_configured_stop_mode();
}
```

This option has a much smaller implementation surface:

- no new ST low-level driver;
- no timer frequency migration;
- no 16-bit ST resolution change;
- no LPTIM compare-write latency issue;
- no make-layer override of ChibiOS `SYSTICKv1`;
- no immediate need to unwind all `STM32_ST_TIM` assumptions.

It is therefore useful as a low-risk transition strategy or as a debug mode.

The tradeoff is power. Any active virtual timer, thread timeout, monitor
timeout, or scheduled kernel alarm prevents Stop entry. Tickless ChibiOS keeps
the ST alarm active whenever the next deadline is armed, so a system with
regular timeouts may spend most idle time in Sleep. Sleep also does not validate
the final Stop-mode timekeeping path.

This policy is safest when:

- short waits dominate and the energy cost is acceptable;
- Stop-mode wake sources are still being debugged;
- the target must preserve the 10 kHz/32-bit TIM2 ST behavior;
- the goal is avoiding missed deadlines, not minimizing idle current.

It is not sufficient when:

- the product requirement is to keep ChibiOS time through Stop;
- long idle periods must reach Stop-mode current;
- virtual timers are routinely active during idle;
- the firmware needs uniform behavior between timed waits and external wake
  sources.

## Recommendation

Implement the Sleep fallback first if a near-term stability bridge is needed,
because it is simple and preserves the current timing model. Treat it as a
policy workaround, not as the final low-power timing architecture.

For production Stop-mode timing, implement the local LPTIM STv3 driver and move
selected STM32U3 targets to a clock-honest ST frequency such as 1024 Hz. That
design directly addresses the root issue: the OS timer must be backed by a
clock domain that continues running through Stop modes.

## Verification Plan

- Build one STM32U3 target using LPTIM3 and one using LPTIM4.
- Confirm the generated build uses the local `hal_st_lld` instead of
  `SYSTICKv1`.
- Verify `chThdSleepMilliseconds()`, virtual timers, monitor timeouts, and
  event waits.
- Exercise alarms scheduled near `now`, near `CH_CFG_ST_TIMEDELTA`, and across
  16-bit wrap.
- Measure interrupt cadence against the 32.768 kHz source.
- Measure compare-write latency on hardware and adjust
  `CH_CFG_ST_TIMEDELTA`.
- Verify Stop-mode current and wake behavior with the ST alarm active.
- Compare against the Sleep fallback to quantify the power benefit of STv3.
