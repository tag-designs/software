#include "app.h"
#include "persistent.h"
#include "tag.pb.h"
#include "tagdata.pb.h"
#include "test_support.h"

#include <stddef.h>

/*
 * Shared self-test orchestrator.
 *
 * Device-specific tests live with the device modules that know how to power,
 * identify, and exercise that hardware. This file only maps monitor TestReq
 * values onto the compiled-in test hooks and records the first failing
 * TestResult.
 */

typedef struct
{
  TestReq request;
  TestResult failure;
  bool (*run)(void);
} TagTestCase;

static const TagTestCase tag_tests[] =
{
#if defined(TAG_SENSOR_ACCEL_ADXL362)
  {RUN_ADXL362, ADXL362_FAILED, tag_test_adxl362},
#endif

#if defined(TAG_SENSOR_ACCEL_LIS2DU12)
  /*
   * The protocol still uses RUN_AIS2 for newer low-power accelerometer tests.
   * Keep that mapping until the monitor protocol grows a generic RUN_ACCEL.
   */
  {RUN_AIS2, LIS2DU12_FAILED, tag_test_lis2du12},
#endif

#if defined(TAG_SENSOR_MAG_AK09940A)
  /*
   * The protocol predates AK09940A and still exposes RUN_MMC5633 for
   * magnetometer tests. Keep the request stable and report the specific
   * AK09940A failure result.
   */
  {RUN_MMC5633, AK09940A_FAILED, tag_test_ak09940a},
#endif

#if defined(TAG_RTC_RV3028)
  {RUN_RTC, RTC_FAILED, tag_test_rtc},
#endif

#if defined(TAG_HAS_EXTERNAL_FLASH)
  {RUN_EXT_FLASH, EXT_FLASH_FAILED, tag_test_external_flash},
#endif

#if defined(TAG_SENSOR_PRESSURE_LPS27)
  {RUN_LPS, LPS_FAILED, tag_test_lps27},
#endif

#if defined(TAG_SENSOR_PRESSURE_LPS22HH)
  {RUN_LPS, LPS_FAILED, tag_test_lps22hh},
#endif

  {RUN_ALL, ALL_PASSED, NULL},
};

static bool test_requested(TestReq request)
{
  return (test_to_run == RUN_ALL) || (test_to_run == request);
}

void test(void)
{
  pState->test_result = TEST_RUNNING;

  for (size_t i = 0; i < sizeof(tag_tests) / sizeof(tag_tests[0]); i++)
  {
    if (tag_tests[i].run == NULL)
    {
      continue;
    }

    if (!test_requested(tag_tests[i].request))
    {
      continue;
    }

    if (!tag_tests[i].run())
    {
      pState->test_result = tag_tests[i].failure;
      return;
    }
  }

  pState->test_result = ALL_PASSED;
}

TestResult testreport(void)
{
  return pState->test_result;
}
