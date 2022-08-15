#include "hal.h"
#include "app.h"
#include "hal_rtc_lld.h"

#define STM32_EXT_LPTIM1_LINE (1U << 0)

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

int32_t GetTimeUnixSec(uint32_t *millis)
{
  RTCDateTime timespec;

  rtcGetTime(&RTCD1, &timespec);
  if (millis)
    *millis = timespec.millisecond % 1000;
  return RTCDateTimeToEpoch(&timespec);
}

int SetTimeUnixSec(int32_t unix_time)
{
  RTCDateTime tim = {0};
  int err = 0;
  if (EpochToRTCDateTime(&tim, unix_time))
    return -1;
  // this order minimizes error if the RTC has already
  // been initialized.
  rtcSetTime(&RTCD1, &tim);
  initRTC();
  if (MSG_OK != setRTCDateTime(&tim))
  {
    //pState->test_result = SET_RTC_FAILED;
  }
  return err;
}

/*
 * Enable/disable timer tick
 * Event mask is set on reset in main.c
 */

void enableSecondsAlarm(void)
{
  chSysLock();

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

  chSysUnlock();
}

/*
 * Enable/disable timer tick
 * Event mask is set on reset in main.c
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
    RTC->CR |= RTC_CR_ALRAIE;
    chSysUnlock();
  }
}

void disableAlarm(unsigned int alarm)
{
  if (alarm < 2)
  {
    chSysLock();
    rtcSetAlarm(&RTCD1, alarm, NULL);
    chSysUnlock();
  }
}

void disableAllAlarms(void)
{
  chSysLock();
  RTC->CR &= ~RTC_CR_ALRAIE;
  rtcSetAlarm(&RTCD1, 0, NULL);
  rtcSetAlarm(&RTCD1, 1, NULL);
  chSysUnlock();
}

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

void disableTicker(void)
{
  rtcSTM32SetPeriodicWakeup(&RTCD1, NULL);
}

void stopMilliseconds(bool spiEnabled,unsigned int ms)
{
  if (MONCONNECTED)
  {
    chThdSleepMilliseconds(ms);
  }
  else
  {
    if (spiEnabled){
       SPI1->CR1 &= ~SPI_CR1_SPE;
    }
    // enable lptim1
    // Enter Stop2 mode

    RCC->APB1ENR1 |= (1 << 31);
    LPTIM1->CR = 1;

    // Set up compare,count registers
    LPTIM1->ARR = ms*STM32_RTC_PRESA_VALUE + 1;
    LPTIM1->CMP = ms*STM32_RTC_PRESA_VALUE; // set compare register
    LPTIM1->CNT = 0;  // reset counter

    EXTI->EMR2 |= STM32_EXT_LPTIM1_LINE;

    // enable interrupts

    LPTIM1->IER = STM32_LPTIM_IER_CMPMIE;
    LPTIM1->CFGR = 0;

    // Start the counter in single mode

    LPTIM1->CR |= STM32_LPTIM_CR_SNGSTRT;

    // go into stop2 mode

    DBGMCU->CR = 0;
    MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, PWR_CR1_LPMS_STOP2);

    SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
    __SEV();
    __WFE();
    __WFE();

    // disable lptim and interrupt

    EXTI->EMR2 &= ~STM32_EXT_LPTIM1_LINE;
    LPTIM1->IER = 0;
    LPTIM1->ICR = 1;
    LPTIM1->CR = 0;
    RCC->APB1ENR1 &= ~(1 << 31);
    CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
    
    // reenable SPI if necessary
    
    if (spiEnabled){
       SPI1->CR1 |= SPI_CR1_SPE;
    }
  }
}
