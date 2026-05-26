/**
 * @file test.c
 * @brief Common self-test runner for tag/family device test tables.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "app.h"
#include "persistent.h"
#include "tag.pb.h"
#include "tagdata.pb.h"
#include "test_support.h"

#include <stdbool.h>
#include <stddef.h>

/** @name Shared self-test orchestration
 * Shared self-test orchestrator.
 *
 * Tag and family devices.c files provide the concrete test list because they
 * own the hardware descriptors. This runner filters by the monitor's requested
 * TestReq and records the first non-ALL_PASSED TestResult.
 * @{
 */

/**
 * @brief Decide whether a table entry should run for the current monitor request.
 *
 * @param[in] request Test request represented by one table entry.
 * @return true when the entry matches the requested test or RUN_ALL.
 */
static bool test_requested(TestReq request)
{
  return (test_to_run == RUN_ALL) || (test_to_run == request);
}

/**
 * @brief Run requested tag self-tests and store the first failure in backup state.
 */
void test(void)
{
  size_t count;
  const TagTestCase *tag_tests = tagTestCases(&count);

  pState->test_result = TEST_RUNNING;

  for (size_t i = 0; i < count; i++)
  {
    if (tag_tests[i].run == NULL)
    {
      continue;
    }

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

/**
 * @brief Return the most recent self-test result.
 *
 * @return Stored self-test result from backup state.
 */
TestResult testreport(void)
{
  return pState->test_result;
}
/** @} */
