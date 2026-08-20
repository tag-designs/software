# `stopMilliseconds()` LPTIM ARR-Match Delay Design

Status: implemented

## Purpose

`stopMilliseconds()` provides short, low-power millisecond delays for common tag
drivers on STM32L432 targets. The current L432 path programs LPTIM1 compare
match (`CMPM`) and sets `ARR` one tick beyond the compare value. For a one-shot
delay, that uses two synchronized timer registers where one terminal
autoreload-match event is sufficient.

This design replaces the compare-match wake with an autoreload-match (`ARRM`)
wake. It also makes the delay clock assumption explicit, moves `CFGR`
configuration before enabling LPTIM1, waits for the autoreload write to latch,
and rejects unrelated `WFE` wakeups before returning to the caller.

The STM32U3 path keeps using `chThdSleepMilliseconds()` and is not changed by
this design.

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

## Proposed Code

The final patch should keep the existing STM32U3 bypass and replace the L432
`#else` body plus compare-specific helpers with ARR-oriented helpers like the
following.

```c
#if !defined(TAG_STOP_LPTIM_HZ)
#define TAG_STOP_LPTIM_HZ 1024U
#endif

#if !defined(TAG_STOP_LPTIM_CFGR)
#define TAG_STOP_LPTIM_CFGR 0U
#endif

#define TAG_STOP_LPTIM_MAX_TICKS 0xFFFFU

static inline uint64_t tagStopMillisecondsToLptimTicks(unsigned int ms)
{
  return (((uint64_t)ms) * TAG_STOP_LPTIM_HZ + 999U) / 1000U;
}

static inline uint32_t tagLptim1ArrMatchFlag(void)
{
#if defined(LPTIM_ISR_ARRM)
  return LPTIM_ISR_ARRM;
#else
  return STM32_LPTIM_ISR_ARRM;
#endif
}

static inline uint32_t tagLptim1ArrOkFlag(void)
{
#if defined(LPTIM_ISR_ARROK)
  return LPTIM_ISR_ARROK;
#else
  return STM32_LPTIM_ISR_ARROK;
#endif
}

static inline void tagLptim1EnableWakeEvent(void)
{
#if defined(EXTI_EMR2_EM32)
  EXTI->EMR2 |= STM32_EXT_LPTIM1_LINE;
#else
  EXTI->EMR1 |= STM32_EXT_LPTIM1_LINE;
#endif
}

static inline void tagLptim1DisableWakeEvent(void)
{
#if defined(EXTI_EMR2_EM32)
  EXTI->EMR2 &= ~STM32_EXT_LPTIM1_LINE;
#else
  EXTI->EMR1 &= ~STM32_EXT_LPTIM1_LINE;
#endif
}

static inline void tagLptim1EnableArrMatchInterrupt(void)
{
#if defined(LPTIM_DIER_ARRMIE)
  LPTIM1->DIER = LPTIM_DIER_ARRMIE;
#else
  LPTIM1->IER = STM32_LPTIM_IER_ARRMIE;
#endif
}

static inline void tagLptim1DisableInterrupts(void)
{
#if defined(LPTIM_DIER_CC1IE)
  LPTIM1->DIER = 0;
#else
  LPTIM1->IER = 0;
#endif
}

static inline void tagLptim1ClearArrMatchFlag(void)
{
#if defined(LPTIM_ICR_ARRMCF)
  LPTIM1->ICR = LPTIM_ICR_ARRMCF;
#else
  LPTIM1->ICR = STM32_LPTIM_ICR_ARRMCF;
#endif
}

static inline void tagLptim1ClearArrOkFlag(void)
{
#if defined(LPTIM_ICR_ARROKCF)
  LPTIM1->ICR = LPTIM_ICR_ARROKCF;
#else
  LPTIM1->ICR = STM32_LPTIM_ICR_ARROKCF;
#endif
}

static inline void tagLptim1ClearPendingWake(void)
{
  uint32_t icr = 0;

#if defined(LPTIM_ICR_CMPMCF)
  icr |= LPTIM_ICR_CMPMCF;
#elif defined(STM32_LPTIM_ICR_CMPMCF)
  icr |= STM32_LPTIM_ICR_CMPMCF;
#endif

#if defined(LPTIM_ICR_ARRMCF)
  icr |= LPTIM_ICR_ARRMCF;
#else
  icr |= STM32_LPTIM_ICR_ARRMCF;
#endif

#if defined(LPTIM_ICR_ARROKCF)
  icr |= LPTIM_ICR_ARROKCF;
#else
  icr |= STM32_LPTIM_ICR_ARROKCF;
#endif

  LPTIM1->ICR = icr;

#if defined(LPTIM1_IRQn)
  NVIC_ClearPendingIRQ(LPTIM1_IRQn);
#endif
}
```

```c
void stopMilliseconds(unsigned int ms)
{
#if defined(STM32U3xx) || defined(STM32U3XX) || defined(STM32U375xx) || defined(STM32U385xx)
  chThdSleepMilliseconds(ms);
#else
  if (ms == 0U)
  {
    return;
  }

  if (monitorIsAttached())
  {
    chThdSleepMilliseconds(ms);
  }
  else
  {
    const uint64_t ticks = tagStopMillisecondsToLptimTicks(ms);

    chDbgAssert(ticks <= TAG_STOP_LPTIM_MAX_TICKS,
                "stopMilliseconds interval exceeds LPTIM range");
    if (ticks > TAG_STOP_LPTIM_MAX_TICKS)
    {
      chThdSleepMilliseconds(ms);
      return;
    }

    tagDisableActiveBusesForStop();

    tagLptim1ClockEnable();
    tagLptim1DisableWakeEvent();
    tagLptim1DisableInterrupts();
    LPTIM1->CR = 0;
    tagLptim1ClearPendingWake();
    LPTIM1->CFGR = TAG_STOP_LPTIM_CFGR;
    LPTIM1->CR = STM32_LPTIM_CR_ENABLE;

    tagLptim1ClearArrOkFlag();
    LPTIM1->ARR = (uint32_t)ticks;
    while ((LPTIM1->ISR & tagLptim1ArrOkFlag()) == 0U) { }
    tagLptim1ClearArrOkFlag();

    tagLptim1ClearArrMatchFlag();
    tagLptim1EnableWakeEvent();
    tagLptim1EnableArrMatchInterrupt();

    LPTIM1->CR |= STM32_LPTIM_CR_SNGSTRT;

    DBGMCU->CR = 0;
    MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, TAG_DELAY_STOP_MODE);
    SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

    __SEV();
    __WFE();
    while ((LPTIM1->ISR & tagLptim1ArrMatchFlag()) == 0U)
    {
      __WFE();
    }

    tagLptim1DisableWakeEvent();
    tagLptim1DisableInterrupts();
    tagLptim1ClearArrMatchFlag();
    LPTIM1->CR = 0;
    tagLptim1ClockDisable();
    CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

    tagEnableActiveBusesAfterStop();
  }
#endif
}
```

## Review Notes

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

## Validation Plan

- Build one active STM32L432 tag target that uses `tag_core`.
- Build one STM32U3 target to confirm the shared helper definitions remain
  portable even though the U3 runtime bypasses the LPTIM stop-delay body.
- On L432 hardware, scope or log a short sequence such as 1 ms, 2 ms, 10 ms,
  100 ms and confirm the delay never returns before the requested interval.
- Verify that monitor-attached behavior still uses `chThdSleepMilliseconds()`.
