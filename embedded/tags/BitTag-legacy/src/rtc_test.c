#include "hal.h"
#include "rtc_api.h"
#include "test_support.h"

#include <stdbool.h>
#include <stdint.h>

TestResult tag_test_rtc(const void *context)
{
  (void)context;

  if (!initRTC())
  {
    return RTC_FAILED;
  }

  RTCDateTime tim;
  if (MSG_OK != getRTCDateTime(&tim))
  {
    return RTC_FAILED;
  }

  rtcSetTime(&RTCD1, &tim);
  return (RTCD1.rtc->PRER == STM32_RTC_PRER_BITS) ? ALL_PASSED : RTC_FAILED;
}
