#ifndef TAG_RTC_API_H
#define TAG_RTC_API_H

#include <stdbool.h>
#include <stdint.h>

#include "ch.h"
#include "hal.h"
#include "custom.h"
#include "mcuconf.h"

#if defined(RV8803_RTC)
#include "rv8803.h"
#endif

#if defined(TAG_RTC_RV3028)
#include "rv3028.h"
#endif

#if defined(RV3032_RTC)
#include "rv3032.h"
#endif

#if defined(RV8803_RTC)
int rv8803_GetReg(enum RV8803Reg reg, uint8_t *val, int num);
#endif

#if defined(TAG_RTC_RV3028)
static inline bool tagRtcInit(void)
{
  return rv3028Init(tagRtcDevice());
}

static inline msg_t tagRtcGetDateTime(RTCDateTime *tm)
{
  return rv3028GetDateTime(tagRtcDevice(), tm);
}

static inline msg_t tagRtcSetDateTime(RTCDateTime *tm)
{
  return rv3028SetDateTime(tagRtcDevice(), tm);
}
#else
msg_t getRTCDateTime(RTCDateTime *tm);
msg_t setRTCDateTime(RTCDateTime *tm);
bool initRTC(void);

static inline bool tagRtcInit(void)
{
  return initRTC();
}

static inline msg_t tagRtcGetDateTime(RTCDateTime *tm)
{
  return getRTCDateTime(tm);
}

static inline msg_t tagRtcSetDateTime(RTCDateTime *tm)
{
  return setRTCDateTime(tm);
}
#endif

enum ALARM_TYPE { ALARM_SECOND, ALARM_MINUTE, ALARM_HOUR };

void enableAlarm(unsigned int alarm, enum ALARM_TYPE atype);
void disableAlarm(unsigned int alarm);
void disableAllAlarms(void);
/*
 * Sleep for a short interval using Stop2 when the monitor is disconnected.
 *
 * The first argument is retained for source compatibility with older callers;
 * the implementation now queries isSpi1On() and suspends/resumes SPI1
 * automatically when the shared SPI1 controller is active.
 */
void stopMilliseconds(bool spiEnabled, unsigned int interval);
void enableTicker(uint16_t interval);
void disableTicker(void);

#if STM32_RTC_PRESA_VALUE * STM32_RTC_PRESS_VALUE == (32 * 1024)
#define RV3028_CLKOUT_VAL 0
#endif
#if STM32_RTC_PRESA_VALUE * STM32_RTC_PRESS_VALUE == 8192
#define RV3028_CLKOUT_VAL 1
#endif
#if STM32_RTC_PRESA_VALUE * STM32_RTC_PRESS_VALUE == 1024
#define RV3028_CLKOUT_VAL 2
#endif
#if STM32_RTC_PRESA_VALUE * STM32_RTC_PRESS_VALUE == 64
#define RV3028_CLKOUT_VAL 3
#endif

#endif
