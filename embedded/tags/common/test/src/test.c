#include "app.h"
#include "persistent.h"
#include "tag.pb.h"
#include "tagdata.pb.h"
#include "test_support.h"

/*
 * Shared self-test orchestrator.
 *
 * Device-specific tests live with the device modules that know how to power,
 * identify, and exercise that hardware. This file only maps monitor TestReq
 * values onto the compiled-in test hooks and records the first failing
 * TestResult.
 */

static bool test_requested(TestReq request)
{
  return (test_to_run == RUN_ALL) || (test_to_run == request);
}

static bool run_test(TestReq request, TestResult failure, bool (*test_fn)(void))
{
  if (!test_requested(request))
  {
    return true;
  }

  if (!test_fn())
  {
    pState->test_result = failure;
    return false;
  }
  return true;
}

void test(void)
{
  pState->test_result = TEST_RUNNING;

#if defined(TAG_SENSOR_ACCEL_ADXL362)
  if (!run_test(RUN_ADXL362, ADXL362_FAILED, tag_test_adxl362))
  {
    return;
  }
#endif

#if defined(TAG_SENSOR_ACCEL_LIS2DU12)
  /*
   * The protocol still uses RUN_AIS2 for newer low-power accelerometer tests.
   * Keep that mapping until the monitor protocol grows a generic RUN_ACCEL.
   */
  if (!run_test(RUN_AIS2, LIS2DU12_FAILED, tag_test_lis2du12))
  {
    return;
  }
#endif

#if defined(TAG_SENSOR_MAG_AK09940A)
  /*
   * The protocol predates AK09940A and still exposes RUN_MMC5633 for
   * magnetometer tests. Keep the request stable and report the specific
   * AK09940A failure result.
   */
  if (!run_test(RUN_MMC5633, AK09940A_FAILED, tag_test_ak09940a))
  {
    return;
  }
#endif

#if defined(TAG_RTC_RV3028)
  if (!run_test(RUN_RTC, RTC_FAILED, tag_test_rtc))
  {
    return;
  }
#endif

#if defined(TAG_HAS_EXTERNAL_FLASH)
  if (!run_test(RUN_EXT_FLASH, EXT_FLASH_FAILED, tag_test_external_flash))
  {
    return;
  }
#endif

#if defined(TAG_SENSOR_PRESSURE_LPS27)
  if (!run_test(RUN_LPS, LPS_FAILED, tag_test_lps27))
  {
    return;
  }
#endif

#if defined(TAG_SENSOR_PRESSURE_LPS22HH)
  if (!run_test(RUN_LPS, LPS_FAILED, tag_test_lps22hh))
  {
    return;
  }
#endif

  pState->test_result = ALL_PASSED;
}

TestResult testreport(void)
{
  return pState->test_result;
}
