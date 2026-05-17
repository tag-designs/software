#include "hal.h"
#include "rtc_api.h"

#include <stdbool.h>
#include <stdint.h>

bool tag_test_rtc(void)
{
  if (!initRTC())
  {
    return false;
  }

  RTCDateTime tim;
  if (MSG_OK != getRTCDateTime(&tim))
  {
    return false;
  }

  rtcSetTime(&RTCD1, &tim);
  return RTCD1.rtc->PRER == STM32_RTC_PRER_BITS;
}
