#include "app.h"
#include "persistent.h"
#include "tag.pb.h"
#include "tagdata.pb.h"
#include "test_support.h"

#include <stddef.h>

/*
 * Historical BitTag self-test runner.
 *
 * BitTag freezes its ADXL362 and RTC drivers locally, so keep its small test
 * dispatch local as well while the shared runner moves to tag-owned test
 * tables in devices.c.
 */

static const TagTestCase tag_tests[] =
{
  {RUN_ADXL362, ADXL362_FAILED, tag_test_adxl362},
  {RUN_RTC, RTC_FAILED, tag_test_rtc},
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
