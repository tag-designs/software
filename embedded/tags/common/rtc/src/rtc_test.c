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

#if defined(TAG_RTC_RV3028) && TAG_RTC_RV3028
#define TAG_RTC_TEST_FAILED RV3028_FAILED
#else
#define TAG_RTC_TEST_FAILED RTC_FAILED
#endif

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
 * dividers, otherwise the configured RTC device's failure result.
 */
TestResult tag_test_rtc(const void *context)
{
  (void)context;

  if (!tagRtcInit())
  {
    return TAG_RTC_TEST_FAILED;
  }

  RTCDateTime tim;
  if (MSG_OK != tagRtcGetDateTime(&tim))
  {
    return TAG_RTC_TEST_FAILED;
  }

  rtcSetTime(&RTCD1, &tim);
  return (RTCD1.rtc->PRER == STM32_RTC_PRER_BITS) ? ALL_PASSED : TAG_RTC_TEST_FAILED;
}
/** @} */
