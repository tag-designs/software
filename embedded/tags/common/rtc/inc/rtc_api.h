/**
 * @file rtc_api.h
 * @brief Common RTC API shim for tag firmware and supported external RTC chips.
 * @author tag firmware authors
 * @date 2026-05-23
 */

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
/**
 * @brief Read one or more RV8803 registers through the legacy RTC binding.
 *
 * @param[in] reg First RV8803 register to read.
 * @param[out] val Destination buffer for register bytes.
 * @param[in] num Number of bytes to read.
 * @return MSG_OK on success or a driver-specific error.
 */
int rv8803_GetReg(enum RV8803Reg reg, uint8_t *val, int num);
#endif

#if defined(TAG_RTC_RV3028)
/** @name RV3028-backed RTC API
 * Inline wrappers bind the generic tag RTC API to the descriptor-based RV3028
 * driver used by newer tags.
 * @{
 */
/**
 * @brief Initialize the configured RV3028 and STM32 RTC divider state.
 *
 * @return true when the RTC oscillator/output configuration is usable.
 */
static inline bool tagRtcInit(void)
{
  return rv3028Init(tagRtcDevice());
}

/**
 * @brief Read date/time from the configured RV3028.
 *
 * @param[out] tm RTC date/time structure to populate.
 * @return MSG_OK on success or a driver-specific error.
 */
static inline msg_t tagRtcGetDateTime(RTCDateTime *tm)
{
  return rv3028GetDateTime(tagRtcDevice(), tm);
}

/**
 * @brief Write date/time to the configured RV3028.
 *
 * @param[in] tm RTC date/time structure to write.
 * @return MSG_OK on success or a driver-specific error.
 */
static inline msg_t tagRtcSetDateTime(RTCDateTime *tm)
{
  return rv3028SetDateTime(tagRtcDevice(), tm);
}
/** @} */
#else
/** @name Legacy RTC API
 * Older RTC drivers expose the historical global init/get/set entry points.
 * The tagRtc* wrappers let core code stay independent of the selected chip.
 * @{
 */
/**
 * @brief Read date/time through the selected legacy RTC driver.
 *
 * @param[out] tm RTC date/time structure to populate.
 * @return MSG_OK on success or a driver-specific error.
 */
msg_t getRTCDateTime(RTCDateTime *tm);

/**
 * @brief Write date/time through the selected legacy RTC driver.
 *
 * @param[in] tm RTC date/time structure to write.
 * @return MSG_OK on success or a driver-specific error.
 */
msg_t setRTCDateTime(RTCDateTime *tm);

/**
 * @brief Initialize the selected legacy external RTC.
 *
 * @return true when the RTC is usable.
 */
bool initRTC(void);

/**
 * @brief Initialize the selected legacy external RTC.
 *
 * @return true when the RTC is usable.
 */
static inline bool tagRtcInit(void)
{
  return initRTC();
}

/**
 * @brief Read date/time from the selected legacy external RTC.
 *
 * @param[out] tm RTC date/time structure to populate.
 * @return MSG_OK on success or a driver-specific error.
 */
static inline msg_t tagRtcGetDateTime(RTCDateTime *tm)
{
  return getRTCDateTime(tm);
}

/**
 * @brief Write date/time to the selected legacy external RTC.
 *
 * @param[in] tm RTC date/time structure to write.
 * @return MSG_OK on success or a driver-specific error.
 */
static inline msg_t tagRtcSetDateTime(RTCDateTime *tm)
{
  return setRTCDateTime(tm);
}
/** @} */
#endif

enum ALARM_TYPE { ALARM_SECOND, ALARM_MINUTE, ALARM_HOUR };

/** @name STM32 RTC alarm and ticker helpers
 * Core state code uses these helpers to request coarse wakeups without knowing
 * the STM32 alarm-register masks directly.
 * @{
 */
/**
 * @brief Enable one RTC alarm with a coarse second/minute/hour mask.
 *
 * @param[in] alarm STM32 alarm index, normally 0 or 1.
 * @param[in] atype Alarm cadence to configure.
 */
void enableAlarm(unsigned int alarm, enum ALARM_TYPE atype);

/**
 * @brief Disable one RTC alarm.
 *
 * @param[in] alarm STM32 alarm index, normally 0 or 1.
 */
void disableAlarm(unsigned int alarm);

/**
 * @brief Disable all STM32 RTC alarm sources.
 */
void disableAllAlarms(void);

/**
 * @brief Sleep for a short interval using Stop2 when the monitor is disconnected.
 *
 * @param[in] interval Delay interval in milliseconds.
 */
void stopMilliseconds(unsigned int interval);

/**
 * @brief Enable the RTC periodic wakeup ticker.
 *
 * @param[in] interval Wakeup interval in seconds.
 */
void enableTicker(uint16_t interval);

/**
 * @brief Disable the RTC periodic wakeup ticker.
 */
void disableTicker(void);
/** @} */

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
