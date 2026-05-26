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
  {RUN_ADXL362, tag_test_adxl362, NULL},
  {RUN_RTC, tag_test_rtc, NULL},
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

    TestResult result = tag_tests[i].run(tag_tests[i].context);
    if (result != ALL_PASSED)
    {
      pState->test_result = result;
      return;
    }
  }

  pState->test_result = ALL_PASSED;
}

TestResult testreport(void)
{
  return pState->test_result;
}
