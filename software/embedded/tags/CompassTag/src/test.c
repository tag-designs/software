#include <stdint.h>
#include "hal.h"
#include "custom.h"
#include "app.h"
#include "tagdata.pb.h"
#include "config.h"
#include "persistent.h"
#include "external_flash.h"
#include "ak09940a.h"
#include "lis2du12.h"

#define DEBUGTEST 0

// no init


static bool failed NOINIT;

#ifdef EXTERNAL_FLASH
static bool test_flash(void){
  bool result;
  ExFlashPwrUp();
  result = (ExCheckID() > -1);
  ExFlashPwrDown();
  return result;
}
#endif

static bool test_rtc(void)
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
  uint32_t prer = RTCD1.rtc->PRER;
  if (prer != STM32_RTC_PRER_BITS)
  {
    return false;
  }
  return true;
}

void test(void)
{
  pState->test_result = TEST_RUNNING;
  failed = false;

  if ((test_to_run == RUN_ALL) ||
      (test_to_run == RUN_RTC))
  {
     if (0 && !test_rtc())
    {
      pState->test_result = RTC_FAILED;
      return;
    } 
  }
#if defined(EXTERNAL_FLASH)
  if ((test_to_run == RUN_ALL)||(test_to_run == RUN_EXT_FLASH))
  {
    if (!test_flash())
    {
      pState->test_result = EXT_FLASH_FAILED;
      return;
    }
  }
#endif

#if defined(USE_AK09440A)
  {
    if (!magTest())
    {
      pState->test_result = AK09940A_FAILED;
      return;
    }
  }
#endif

#if defined(USE_LIS2DU12)
  {
    if (!accelTest())
    {
      pState->test_result = LIS2DU12_FAILED;
      return;
    }
  }
#endif



  pState->test_result = ALL_PASSED;
}

TestResult testreport()
{
  return (pState->test_result);
}
