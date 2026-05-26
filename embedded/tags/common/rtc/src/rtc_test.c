/**
 * @file rtc_test.c
 * @brief Shared RTC self-test implementation.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"
#include "rtc_api.h"
#include "test_support.h"

#include <stdbool.h>
#include <stdint.h>

/** @name RTC self-test
 * Test helper used by tag self-test tables to verify external RTC access and
 * STM32 divider compatibility.
 * @{
 */
/**
 * @brief Test RTC initialization and date/time readout.
 *
 * @param[in] context Unused.
 * @return ALL_PASSED when the RTC initializes, reads, and leaves expected
 * dividers, otherwise RTC_FAILED.
 */
TestResult tag_test_rtc(const void *context)
{
  (void)context;

  if (!tagRtcInit())
  {
    return RTC_FAILED;
  }

  RTCDateTime tim;
  if (MSG_OK != tagRtcGetDateTime(&tim))
  {
    return RTC_FAILED;
  }

  rtcSetTime(&RTCD1, &tim);
  return (RTCD1.rtc->PRER == STM32_RTC_PRER_BITS) ? ALL_PASSED : RTC_FAILED;
}
/** @} */
