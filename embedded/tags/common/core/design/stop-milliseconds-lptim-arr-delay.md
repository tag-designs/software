---
type: design
status: current
summary: How STM32L432 stopMilliseconds() delays on a single LPTIM1 autoreload match at 1024 Hz, with spurious-wake filtering.
---

# `stopMilliseconds()` LPTIM ARR-Match Delay Design

## Purpose

`stopMilliseconds()` (`common/core/src/time.c`) provides short, low-power
millisecond delays for common tag drivers on STM32L432 targets. It wakes on a
single LPTIM1 autoreload match (`ARRM`) in single-shot mode, so a one-shot delay
needs one synchronized register write, `ARR`. An earlier version programmed a
compare match (`CMPM`) with `ARR` one tick beyond it, which needed two.
The helper names its delay clock explicitly, programs `CFGR` before enabling
LPTIM1, waits for the `ARR` write to latch, and ignores unrelated `WFE`
wakeups before returning.

On STM32U3 targets `stopMilliseconds()` is `chThdSleepMilliseconds()`, and
nothing here applies. L432 targets that define `TAG_STOP_RTC_TICKER` use an
RTC alarm A ticker instead of LPTIM1; see
[0009](../../../../../docs/decisions/0009-prestag-stop-delay-rtc-alarm-a.md).
On the LPTIM path, a monitor attachment also makes it fall back to
`chThdSleepMilliseconds()`.

## Assumptions

- Active STM32L432 tag targets run the LPTIM1 stop-delay counter at 1024 Hz.
- LPTIM1 is used only as a one-shot delay source while `stopMilliseconds()` is
  executing.
- Delay requests are expected to be short driver waits, but the public helper
  should still guard the 16-bit LPTIM range.
- Any target that changes the LPTIM1 delay clock must update the named delay
  clock constant rather than relying on RTC prescaler symbols by accident.

## Design

Use `ARR` as the terminal count and enable the autoreload-match event:

1. Convert milliseconds to LPTIM ticks using ceiling division so the helper does
   not wake early.
2. Reject or fall back for requests that exceed the 16-bit LPTIM counter range.
3. Disable LPTIM1, clear stale match/update state, program `CFGR`, then enable
   LPTIM1.
4. Clear stale `ARROK`, write `ARR`, and wait until `ARROK` confirms the value
   latched into the timer clock domain.
5. Enable the LPTIM EXTI event and the `ARRM` interrupt/event source.
6. Start the counter in single-shot mode.
7. Enter the configured stop mode with `WFE`.
8. Loop until the LPTIM `ARRM` flag is observed, ignoring unrelated wake events.
9. Disable the event source, clear `ARRM`, disable LPTIM1, and restore buses.

This removes the compare-register write, the compare update synchronization
point, and the misleading `CNT = 0` write. Disabling and re-enabling LPTIM1 is
the counter reset mechanism.

## Code

The implementation is in `common/core/src/time.c`: `stopMilliseconds()` and
the `tagLptim1*` helpers beside it. Requests that exceed the 16-bit counter
range assert in debug builds and fall back to `chThdSleepMilliseconds()`.

## Notes

- `TAG_STOP_LPTIM_HZ` is intentionally separate from
  `STM32_RTC_PRESS_VALUE`. The RTC subsecond rate can describe the RTC, but the
  stop-delay conversion should name the LPTIM delay clock directly.
- `TAG_STOP_LPTIM_CFGR` defaults to zero to preserve the current L432 register
  setup. If a future target obtains 1024 Hz through an LPTIM prescaler, it
  should override this value beside the target clock configuration.
- The fallback for out-of-range requests preserves timing correctness in
  release builds. It gives up the low-power delay for requests that do not fit
  the one-shot LPTIM range.
- The wait loop checks the LPTIM `ARRM` flag rather than assuming the first
  post-flush `WFE` wake belongs to LPTIM1.
