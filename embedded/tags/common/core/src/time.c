/**
 * @file time.c
 * @brief RTC calendar conversion, alarms, ticker, and stop-mode delay support.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"
#include "hal_rtc_lld.h"

#include "core_types.h"
#include "custom.h"
#include "power.h"
#include "rtc_api.h"
#include "timekeeping.h"
#include "core_sync.h"
#include "phase_probe.h"

#define STM32_EXT_LPTIM1_LINE (1U << 0)

/**
 * @def     TAG_STOP_RTC_TICKER
 * @brief   Use RTC Alarm A as a free-running sub-second tick for stop delays.
 *
 * @details The per-delay LPTIM path rewrites @c ARR and busy-waits for
 *          @c ARROK before every wait. LPTIM registers only latch on kernel
 *          clock edges, and this board's LSE is 1024 Hz, so that wait measured
 *          6.3-7.1 ms of Run current for a 5 ms request -- longer than the
 *          delay it was arming, which is why short waits never slept at all.
 *
 *          Alarm A has no such cost on the hot path. It is configured once to
 *          match on a sub-second mask and left enabled; a delay only sets and
 *          clears @c ALRAIE, which is an ordinary @c RTC_CR bit behind write
 *          protection with no synchronisation flag to wait on. @c ALRAWF is
 *          touched only during setup.
 *
 * @note    Alarm B is untouched: @c Configured and @c Hibernating keep using it
 *          for their minute wake. The two share EXTI line 18, so a B match
 *          during a delay also wakes the loop; the loop tests @c ALRAF rather
 *          than the EXTI pending bit and simply sleeps again, which costs a
 *          wake but cannot miscount.
 */
#if !defined(TAG_STOP_RTC_TICKER)
#define TAG_STOP_RTC_TICKER 0
#endif

/**
 * @brief   Sub-second alarm mask selecting the tick period.
 *
 * @details @c MASKSS = n compares @c SS[n-1:0], so with a zero target the alarm
 *          fires every 2^n sub-second counts. @c SS decrements at
 *          RTCCLK/(PREDIV_A+1) = 1024 Hz here, so 1 is every second count:
 *          1.95 ms, and the finest available. @c MASKSS = 0 is not finer -- it
 *          disables sub-second comparison altogether and fires once a second.
 */
#if !defined(TAG_STOP_RTC_MASKSS)
#define TAG_STOP_RTC_MASKSS 1U
#endif

/** @brief Alarm A tick frequency, in Hz. */
#define TAG_STOP_RTC_TICK_HZ (1024U / (1U << TAG_STOP_RTC_MASKSS))

/** @brief Bound on polled waits, so no delay can hang the caller. */
#define TAG_STOP_RTC_SPIN_LIMIT 100000U

/**
 * @brief LPTIM1 counter frequency used by the STM32L432 stop-delay path.
 */
#if !defined(TAG_STOP_LPTIM_HZ)
#define TAG_STOP_LPTIM_HZ 1024U
#endif

/**
 * @brief LPTIM1 configuration used before arming the one-shot stop delay.
 */
#if !defined(TAG_STOP_LPTIM_CFGR)
#define TAG_STOP_LPTIM_CFGR 0U
#endif

/**
 * @brief Maximum delay count representable by the 16-bit LPTIM ARR register.
 */
#define TAG_STOP_LPTIM_MAX_TICKS 0xFFFFU

#if !defined(USE_STOP1)
#define USE_STOP1 0
#endif

#if !defined(USE_STOP1_DELAY)
#define USE_STOP1_DELAY USE_STOP1
#endif

#if defined(USE_STOP1_DELAY) && USE_STOP1_DELAY
#if defined(PWR_CR1_LPMS_STOP1)
#define TAG_DELAY_STOP_MODE PWR_CR1_LPMS_STOP1
#else
#define TAG_DELAY_STOP_MODE PWR_CR1_LPMS_0
#endif
#else
#if defined(PWR_CR1_LPMS_STOP2)
#define TAG_DELAY_STOP_MODE PWR_CR1_LPMS_STOP2
#else
#define TAG_DELAY_STOP_MODE PWR_CR1_LPMS_1
#endif
#endif

// chibios keeps time since 1980, seconds to 1/1/1980

#define SECSPERDAY (24 * 3600)
#define YEAR_BASE 1980
#define EPOCH_YEAR 1970
#define EPOCH_WDAY 4

// time between 1/1/1970 and 1/1/2000

#define SECONDS_TO_BASE 315532800
// 946684800
// to 1/1/2000 946684800

// macros for converting date to epoch

#define isleap(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)
#define YEARSIZE(y) (isleap(y) ? 366 : 365)

static const uint32_t mon_lengths[2][12] = {
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}};

static inline void tagLptim1ClockEnable(void)
{
#if defined(RCC_APB3ENR_LPTIM1EN)
  RCC->APB3ENR |= RCC_APB3ENR_LPTIM1EN;
#elif defined(RCC_APB1ENR1_LPTIM1EN)
  RCC->APB1ENR1 |= RCC_APB1ENR1_LPTIM1EN;
#endif
}

static inline void tagLptim1ClockDisable(void)
{
#if defined(RCC_APB3ENR_LPTIM1EN)
  RCC->APB3ENR &= ~RCC_APB3ENR_LPTIM1EN;
#elif defined(RCC_APB1ENR1_LPTIM1EN)
  RCC->APB1ENR1 &= ~RCC_APB1ENR1_LPTIM1EN;
#endif
}

/**
 * @brief Convert milliseconds to stop-delay LPTIM ticks without waking early.
 *
 * @param[in] ms Delay interval in milliseconds.
 * @return Ceiling-rounded LPTIM tick count at TAG_STOP_LPTIM_HZ.
 */
static inline uint64_t tagStopMillisecondsToLptimTicks(unsigned int ms)
{
  return (((uint64_t)ms) * TAG_STOP_LPTIM_HZ + 999U) / 1000U;
}

/**
 * @brief Return the vendor-header-specific ARR match status bit.
 *
 * @return Bit mask to test in LPTIM1->ISR.
 */
static inline uint32_t tagLptim1ArrMatchFlag(void)
{
#if defined(LPTIM_ISR_ARRM)
  return LPTIM_ISR_ARRM;
#else
  return STM32_LPTIM_ISR_ARRM;
#endif
}

/**
 * @brief Return the vendor-header-specific ARR update synchronization bit.
 *
 * @return Bit mask to test in LPTIM1->ISR.
 */
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
#if defined(LPTIM_DIER_ARRMIE)
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

#if defined(LPTIM_ICR_CC1CF)
  icr |= LPTIM_ICR_CC1CF;
#elif defined(LPTIM_ICR_CMPMCF)
  icr |= LPTIM_ICR_CMPMCF;
#else
  icr |= STM32_LPTIM_ICR_CMPMCF;
#endif

#if defined(LPTIM_ICR_ARRMCF)
  icr |= LPTIM_ICR_ARRMCF;
#elif defined(STM32_LPTIM_ICR_ARRMCF)
  icr |= STM32_LPTIM_ICR_ARRMCF;
#endif

#if defined(LPTIM_ICR_ARROKCF)
  icr |= LPTIM_ICR_ARROKCF;
#elif defined(STM32_LPTIM_ICR_ARROKCF)
  icr |= STM32_LPTIM_ICR_ARROKCF;
#endif

  LPTIM1->ICR = icr;

#if defined(LPTIM1_IRQn)
  NVIC_ClearPendingIRQ(LPTIM1_IRQn);
#endif
}

/** @name RTC calendar conversion
 * Conversion helpers isolate the STM32 RTC calendar base from the Unix epoch
 * used by logs, monitor messages, and protobuf configuration.
 * @{
 */
/**
 * @brief Convert an STM32 RTCDateTime value to Unix seconds.
 *
 * @param[in] tim RTC calendar value to convert.
 * @return Seconds since 1970-01-01 UTC.
 */
static time_t RTCDateTimeToEpoch(RTCDateTime *tim)
{
  int32_t year, month, seconds;
  int32_t days = tim->day - 1;

  // walk time forward from base year

  for (year = 0; year < tim->year; year++)
    days += YEARSIZE(year + YEAR_BASE);

  for (month = 1; month < tim->month; month++)
    days += mon_lengths[isleap(year)][month - 1];

  // add up the seconds

  seconds = SECONDS_TO_BASE + tim->millisecond / 1000;
  //    60*(tim->tm_min + 60*(tim->tm_hour));

  return seconds + (days * SECSPERDAY);
}

/**
 * @brief Convert Unix seconds into an STM32 RTCDateTime value.
 *
 * @param[out] tim RTC calendar value to populate.
 * @param[in] epoch Seconds since 1970-01-01 UTC.
 * @return 0 on success, or -1 when the epoch predates the supported base.
 */
static int EpochToRTCDateTime(RTCDateTime *tim, int32_t epoch)
{
  uint32_t dayclock, dayno, month;
  uint32_t year = EPOCH_YEAR;

  if (epoch < SECONDS_TO_BASE)
    return -1;

  dayclock = epoch % SECSPERDAY;
  dayno = epoch / SECSPERDAY;

  tim->millisecond = dayclock * 1000;
  tim->dayofweek = ((dayno + 4) % 7) + 1;

  // walk years forward

  while (dayno >= YEARSIZE(year))
  {
    dayno -= YEARSIZE(year);
    year++;
  }

  // walk months forward

  const uint32_t *ip = mon_lengths[isleap(year)];

  month = 0;
  while (dayno >= ip[month])
  {
    dayno -= ip[month];
    month++;
  }

  // use 1 based counting

  tim->year = year - YEAR_BASE;
  tim->month = month + 1;
  tim->day = dayno + 1;
  return 0;
}
/** @} */

/** @name RTC timekeeping
 * Public time helpers used by logging, monitor status, and configuration.
 * @{
 */
/**
 * @brief Read current RTC time as Unix seconds.
 *
 * @param[out] millis Optional millisecond remainder from the RTC calendar time.
 * @return Current time as seconds since 1970-01-01 UTC.
 */
int32_t GetTimeUnixSec(uint32_t *millis)
{
  RTCDateTime timespec;

  rtcGetTime(&RTCD1, &timespec);
  if (millis)
    *millis = timespec.millisecond % 1000;
  return RTCDateTimeToEpoch(&timespec);
}

/**
 * @brief Set the RTC from Unix seconds and synchronize the external RTC device.
 *
 * @param[in] unix_time Seconds since 1970-01-01 UTC.
 * @return 0 on success, or -1 when unix_time cannot be represented.
 */
int SetTimeUnixSec(int32_t unix_time)
{
  RTCDateTime tim = {0};
  int err = 0;
  if (EpochToRTCDateTime(&tim, unix_time))
    return -1;
  // this order minimizes error if the RTC has already
  // been initialized.
  rtcSetTime(&RTCD1, &tim);
  tagRtcInit();
  if (!tagRtcApplyClockCorrection())
  {
#if defined(TAG_RTC_STM32U3_COMPAT) && TAG_RTC_STM32U3_COMPAT
    return -1;
#endif
  }
  if (MSG_OK != tagRtcSetDateTime(&tim))
  {
    //pState->test_result = SET_RTC_FAILED;
#if defined(TAG_RTC_STM32U3_COMPAT) && TAG_RTC_STM32U3_COMPAT
    return -1;
#endif
  }
  /*
   * The clock has just been set from a known-good source, so any doubt recorded
   * at boot no longer applies. Without this, a boot that could not verify the
   * external RTC left clockTrusted false for the whole session, which held off
   * the immediate-start path and delayed every configured start to the next
   * minute alarm.
   */
  clockTrusted = true;
  return err;
}
/** @} */

/** @name RTC alarms and ticker
 * Enable/disable timer tick
 * Event mask is set on reset in main.c
 * @{
 */

/**
 * @brief Configure RTC alarm A to wake once per second.
 */
void enableSecondsAlarm(void)
{
 

  // turn off existing alarms

  rtcSetAlarm(&RTCD1, 0, NULL);
  rtcSetAlarm(&RTCD1, 1, NULL);

  // program alarm A for 1 second ticks

  RTCAlarm alarmspec;
  alarmspec.alrmr =
      (RTC_ALRMAR_MSK4 | RTC_ALRMAR_MSK3 | RTC_ALRMAR_MSK2 | RTC_ALRMAR_MSK1);
  rtcSetAlarm(&RTCD1, 0, &alarmspec);

  // enable interrupt flag in RTC

  RTC->CR |= RTC_CR_ALRAIE;


}

/**
 * @brief Configure one RTC alarm for the requested cadence.
 *
 * @param[in] alarm RTC alarm index, 0 or 1.
 * @param[in] atype Alarm cadence to program.
 */
void enableAlarm(unsigned int alarm, enum ALARM_TYPE atype)
{
  if (alarm < 2)
  {
    RTCAlarm alarmspec;
    switch (atype)
    {
    case ALARM_SECOND:
      alarmspec.alrmr = (RTC_ALRMAR_MSK4 | RTC_ALRMAR_MSK3 | RTC_ALRMAR_MSK2 | RTC_ALRMAR_MSK1);
      break;
    case ALARM_MINUTE:
      alarmspec.alrmr = (RTC_ALRMAR_MSK4 | RTC_ALRMAR_MSK3 | RTC_ALRMAR_MSK2);
      break;
    case ALARM_HOUR:
      alarmspec.alrmr = (RTC_ALRMAR_MSK4 | RTC_ALRMAR_MSK3 | RTC_ALRMAR_MSK2);
      break;
    default:
      return;
    }

    rtcSetAlarm(&RTCD1, alarm, &alarmspec);
    //RTC->CR |= RTC_CR_ALRAIE;
   
  }
}

/**
 * @brief Disable one RTC alarm.
 *
 * @param[in] alarm RTC alarm index, 0 or 1.
 */
void disableAlarm(unsigned int alarm)
{
  if (alarm < 2)
  {
  
    rtcSetAlarm(&RTCD1, alarm, NULL);
  
  }
}


// this is a bit of a mess and doesn't really work correctly
  void delayAlarmEpoch(unsigned int alarm, unsigned int epoch)

{
  if (alarm < 2)
  {
    RTCAlarm alarmspec;
    uint32_t wake = epoch;
    RTCDateTime tim_spec;
    EpochToRTCDateTime(&tim_spec, wake);
    // 2. Safely unpack individual components from the ChibiOS fields
    uint32_t seconds = (tim_spec.millisecond / 1000) % 60;
    uint32_t minutes = (tim_spec.millisecond / 60000) % 60;
    uint32_t hours   = (tim_spec.millisecond / 3600000) % 24;

    // 3. Populate the register structure natively using ChibiOS/ST Macros
    alarmspec.alrmr = RTC_ALRM_MSK4 |                          // Mask 4: Ignore date/day match
                      RTC_ALRM_HT(hours / 10)   | RTC_ALRM_HU(hours % 10)   |
                      RTC_ALRM_MNT(minutes / 10) | RTC_ALRM_MNU(minutes % 10) |
                      RTC_ALRM_ST(seconds / 10)  | RTC_ALRM_SU(seconds % 10);

    // 4. Pass the specification to the ChibiOS driver (0 represents Alarm A)
    rtcSetAlarm(&RTCD1, 0, &alarmspec);
    //rtcSetAlarm(&RTCD1, alarm, &alarmspec);

  }
}

/**
 * @brief Disable all RTC alarm interrupts and alarm slots.
 */
void disableAllAlarms(void)
{
 
  //RTC->CR &= ~RTC_CR_ALRAIE;
  rtcSetAlarm(&RTCD1, 0, NULL);
  rtcSetAlarm(&RTCD1, 1, NULL);
}

/**
 * @brief Enable the RTC periodic wakeup ticker.
 *
 * @param[in] secs Wakeup interval in seconds.
 */
void enableTicker(uint16_t secs)
{
  RTCWakeup wakeup;
  if (secs < 1)
    return;
  /* if ((RTC->CR & RTC_CR_WUTE) &&
      ((RTC->WUTR & 0xffff) + 1 == (secs)))
    return; */
  wakeup.wutr = (4 << 16) + (secs - 1);
  rtcSTM32SetPeriodicWakeup(&RTCD1, &wakeup);
}

/**
 * @brief Disable the RTC periodic wakeup ticker.
 */
void disableTicker(void)
{
  rtcSTM32SetPeriodicWakeup(&RTCD1, NULL);
}
/** @} */

/** @name Low-power delay
 * Delay helper used by drivers when they need milliseconds to pass without
 * keeping active buses powered through stop-mode delays unnecessarily.
 * @{
 */
#if TAG_STOP_RTC_TICKER

/** @brief RTC write-protection key sequence. */
static inline void tagRtcWriteUnlock(void) { RTC->WPR = 0xCAU; RTC->WPR = 0x53U; }
static inline void tagRtcWriteLock(void)   { RTC->WPR = 0xFFU; }

/** @brief EXTI bit for the RTC alarm line, shared by Alarm A and Alarm B. */
#define TAG_RTC_ALARM_EXTI_BIT (1U << STM32_RTC_ALARM_EXTI)

void tagStopRtcTickerInit(void)
{
  uint32_t limit = TAG_STOP_RTC_SPIN_LIMIT;

  tagRtcWriteUnlock();
  RTC->CR &= ~RTC_CR_ALRAE;
  /* The only synchronisation wait in the scheme, and it happens once per run. */
  while (((RTC->ISR & RTC_ISR_ALRAWF) == 0U) && (limit-- > 0U)) { }

  /* Mask the whole calendar; the sub-second mask alone sets the tick period. */
  RTC->ALRMAR = RTC_ALRMAR_MSK1 | RTC_ALRMAR_MSK2 |
                RTC_ALRMAR_MSK3 | RTC_ALRMAR_MSK4;
  RTC->ALRMASSR = ((uint32_t)TAG_STOP_RTC_MASKSS) << RTC_ALRMASSR_MASKSS_Pos;
  RTC->CR |= RTC_CR_ALRAE;
  RTC->CR &= ~RTC_CR_ALRAIE;      /* silent until a delay arms it */
  tagRtcWriteLock();

  EXTI->RTSR1 |= TAG_RTC_ALARM_EXTI_BIT;
  EXTI->EMR1 &= ~TAG_RTC_ALARM_EXTI_BIT;
}
#endif /* TAG_STOP_RTC_TICKER */

#if defined(STM32U3xx) || defined(STM32U3XX) || defined(STM32U375xx) || defined(STM32U385xx)

/**
 * @brief Sleep for a short interval.
 *
 * @details STM32U3 targets have no stop-mode delay path; the ordinary RTOS
 *          sleep is used throughout.
 *
 * @param[in] ms Delay interval in milliseconds.
 */
void stopMilliseconds(unsigned int ms)
{
  chThdSleepMilliseconds(ms);
}

#elif TAG_STOP_RTC_TICKER

/**
 * @brief Sleep for a short interval using the configured stop mode.
 *
 * @details Counts matches of the free-running RTC Alarm A ticker, so nothing is
 *          armed per call beyond @c ALRAIE and there is no synchronisation
 *          wait on this path. See @c TAG_STOP_RTC_TICKER.
 *          Falls back to the RTOS sleep while a monitor session is open: the
 *          tag must stay responsive and must not enter a stop mode.
 *
 * @param[in] ms Delay interval in milliseconds.
 */
void stopMilliseconds(unsigned int ms)
{
  if (ms == 0U)
  {
    return;
  }

  /*
   * The RTOS sleep covers three cases, all of which must keep the system tick
   * running: a monitor session, a pending virtual timer (Stop 2 halts TIM2, so
   * the timer would never fire), and any delay taken outside a run, when
   * Running() has not armed the ticker.
   */
  sysinterval_t vt_next;
  bool vt_pending;

  chSysLock();
  vt_pending = chVTGetTimersStateI(&vt_next);
  chSysUnlock();

  if (monitorIsAttached() || vt_pending || ((RTC->CR & RTC_CR_ALRAE) == 0U))
  {
    chThdSleepMilliseconds(ms);
    return;
  }

  /* One extra tick: the first match lands anywhere inside the current one. */
  const uint32_t ticks =
      (uint32_t)((((uint64_t)ms) * TAG_STOP_RTC_TICK_HZ + 999U) / 1000U) + 1U;

  tagDisableActiveBusesForStop();

  /*
   * ChibiOS enables the interrupt on this line for Alarm B, and it is shared,
   * so the alarm must be masked there or every tick runs the RTC ISR instead
   * of waking silently. Restored on the way out.
   */
  const uint32_t imr_saved = EXTI->IMR1 & TAG_RTC_ALARM_EXTI_BIT;
  EXTI->IMR1 &= ~TAG_RTC_ALARM_EXTI_BIT;
  EXTI->EMR1 |= TAG_RTC_ALARM_EXTI_BIT;

  tagRtcWriteUnlock();
  RTC->ISR &= ~RTC_ISR_ALRAF;
  RTC->CR |= RTC_CR_ALRAIE;

  DBGMCU->CR = 0;
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, TAG_DELAY_STOP_MODE);
  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
  /*
  * Commit SLEEPDEEP and LPMS before sleeping. Without the barrier the
  * store can still be buffered when the WFE executes, so the core enters
  * plain Sleep with the old SLEEPDEEP and no stop mode happens -- while a
  * later read of SCB->SCR still shows 1, which is what made this
  * invisible. pwr-l432.c's terminal sleep has always had this barrier.
  */
  __DSB();



  __SEV();
  __WFE();


  for (uint32_t counted = 0U; counted < ticks; )
  {
    __WFE();
    if ((RTC->ISR & RTC_ISR_ALRAF) != 0U)
    {
      RTC->ISR &= ~RTC_ISR_ALRAF;
      counted++;
    }
  }


  CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
  RTC->CR &= ~RTC_CR_ALRAIE;
  tagRtcWriteLock();

  EXTI->EMR1 &= ~TAG_RTC_ALARM_EXTI_BIT;
  EXTI->PR1 = TAG_RTC_ALARM_EXTI_BIT;
  EXTI->IMR1 |= imr_saved;

  tagEnableActiveBusesAfterStop();
}

#else

/**
 * @brief Sleep for a short interval using the configured stop mode.
 *
 * @details Re-arms LPTIM1 as a one-shot for each wait. Writing @c ARR costs an
 *          @c ARROK synchronisation wait at the LPTIM kernel clock, which is
 *          why targets on a slow LSE prefer @c TAG_STOP_RTC_TICKER.
 *          Falls back to the RTOS sleep while a monitor session is open: the
 *          tag must stay responsive and must not enter a stop mode.
 *
 * @param[in] ms Delay interval in milliseconds.
 */
void stopMilliseconds(unsigned int ms)
{
  if (ms == 0U)
  {
    return;
  }

  if (monitorIsAttached())
  {
    chThdSleepMilliseconds(ms);
    return;
  }

  const uint64_t ticks = tagStopMillisecondsToLptimTicks(ms);

  chDbgAssert(ticks <= TAG_STOP_LPTIM_MAX_TICKS,
              "stopMilliseconds interval exceeds LPTIM range");
  if (ticks > TAG_STOP_LPTIM_MAX_TICKS)
  {
    chThdSleepMilliseconds(ms);
    return;
  }

  tagDisableActiveBusesForStop();
#if defined(TAG_PHASE_PROBE) && TAG_PHASE_PROBE
  if (tagPhaseProbe.magic == TAG_PHASE_PROBE_MAGIC && tagPhaseProbe.open == 1U && (tagPhaseProbe.aux[tagPhaseProbe.seq % TAG_PHASE_PROBE_SLOTS][4] & 3U) == 0U)
    tagPhaseProbeMark(25);
#endif  /* call #1: buses disabled */

  tagLptim1ClockEnable();
  tagLptim1DisableWakeEvent();
  tagLptim1DisableInterrupts();

  /* Disabling LPTIM1 resets the counter; CNT is read-only on this part. */
  LPTIM1->CR = 0;
  tagLptim1ClearPendingWake();

  /* CFGR fields are write-protected while ENABLE is set. */
  LPTIM1->CFGR = TAG_STOP_LPTIM_CFGR;
  LPTIM1->CR = STM32_LPTIM_CR_ENABLE;
#if defined(TAG_PHASE_PROBE) && TAG_PHASE_PROBE
  if (tagPhaseProbe.magic == TAG_PHASE_PROBE_MAGIC && tagPhaseProbe.open == 1U && (tagPhaseProbe.aux[tagPhaseProbe.seq % TAG_PHASE_PROBE_SLOTS][4] & 3U) == 0U)
    tagPhaseProbeMark(26);
#endif  /* call #1: LPTIM1 enabled, ARR about to be written */

  /*
   * Use ARR as the one-shot terminal count.  Clear ARROK before writing so
   * the synchronization wait cannot be satisfied by a stale update flag.
   */
  tagLptim1ClearArrOkFlag();
  LPTIM1->ARR = (uint32_t)ticks;
  while ((LPTIM1->ISR & tagLptim1ArrOkFlag()) == 0U) { }
#if defined(TAG_PHASE_PROBE) && TAG_PHASE_PROBE
  if (tagPhaseProbe.magic == TAG_PHASE_PROBE_MAGIC && tagPhaseProbe.open == 1U && (tagPhaseProbe.aux[tagPhaseProbe.seq % TAG_PHASE_PROBE_SLOTS][4] & 3U) == 0U)
    tagPhaseProbeMark(27);
#endif  /* call #1: ARROK observed */
  tagLptim1ClearArrOkFlag();
#if defined(TAG_PHASE_PROBE) && TAG_PHASE_PROBE
  tagPhaseProbeMarkIfOpen(17 + 2U * (tagPhaseProbe.aux[tagPhaseProbe.seq % TAG_PHASE_PROBE_SLOTS][4] & 3U)); /* ARR latched: 17/19/21 for 1st/2nd/3rd call */
#endif

  tagLptim1ClearArrMatchFlag();
  tagLptim1EnableWakeEvent();
  tagLptim1EnableArrMatchInterrupt();

  LPTIM1->CR |= STM32_LPTIM_CR_SNGSTRT;

  // go into the configured stop mode

  DBGMCU->CR = 0;
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, TAG_DELAY_STOP_MODE);

  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
  /*
  * Commit SLEEPDEEP and LPMS before sleeping. Without the barrier the
  * store can still be buffered when the WFE executes, so the core enters
  * plain Sleep with the old SLEEPDEEP and no stop mode happens -- while a
  * later read of SCB->SCR still shows 1, which is what made this
  * invisible. pwr-l432.c's terminal sleep has always had this barrier.
  */
  __DSB();
  __SEV();
  __WFE();

  /* WFE can return for unrelated events; only ARRM completes this delay. */
#if defined(TAG_PHASE_PROBE) && TAG_PHASE_PROBE
  uint32_t probe_wfe_iters = 0U;
  while ((LPTIM1->ISR & tagLptim1ArrMatchFlag()) == 0U)
  {
    __WFE();
    probe_wfe_iters++;
  }
#else
  while ((LPTIM1->ISR & tagLptim1ArrMatchFlag()) == 0U)
  {
    __WFE();
  }
#endif
#if defined(TAG_PHASE_PROBE) && TAG_PHASE_PROBE
  {
    uint32_t ps = tagPhaseProbe.seq % TAG_PHASE_PROBE_SLOTS;
    uint32_t call = tagPhaseProbe.aux[ps][4] & 3U;
    tagPhaseProbeMarkIfOpen(18 + 2U * call);          /* ARRM seen: 18/20/22 */
    if (tagPhaseProbe.magic == TAG_PHASE_PROBE_MAGIC && tagPhaseProbe.open == 1U) {
      tagPhaseProbe.aux[ps][5 + (call > 2U ? 2U : call)] = probe_wfe_iters; /* aux5..7 */
      tagPhaseProbe.aux[ps][4] = call + 1U;
    }
  }
#endif

  // disable lptim and interrupt

  tagLptim1DisableWakeEvent();
  tagLptim1DisableInterrupts();
  tagLptim1ClearArrMatchFlag();
  LPTIM1->CR = 0;
  tagLptim1ClockDisable();
  CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  tagEnableActiveBusesAfterStop();
}

#endif
/** @} */
