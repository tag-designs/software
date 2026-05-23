/**
 * @file timekeeping.h
 * @brief RTC time conversion and low-power millisecond delay API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_TIMEKEEPING_H
#define TAG_CORE_TIMEKEEPING_H

#include <stdint.h>

/** @name RTC timekeeping
 * Helpers used by monitor, logging, and state-machine code to work in Unix
 * seconds while the STM32 RTC uses its native calendar representation.
 * @{
 */
/**
 * @brief Read current RTC time as Unix seconds.
 *
 * @param[out] millis Optional millisecond remainder from the RTC calendar time.
 * @return Current time as seconds since 1970-01-01 UTC.
 */
int32_t GetTimeUnixSec(uint32_t *millis);

/**
 * @brief Set the RTC from Unix seconds and synchronize the external RTC device.
 *
 * @param[in] unix_time Seconds since 1970-01-01 UTC.
 * @return 0 on success, or -1 when unix_time cannot be represented.
 */
int SetTimeUnixSec(int32_t unix_time);

/**
 * @brief Sleep for a short interval using Stop2 when no monitor is attached.
 *
 * @param[in] interval Delay interval in milliseconds.
 */
void stopMilliseconds(unsigned int interval);
/** @} */

#endif
