/**
 * @file    startchecks.cc
 * @brief   The start checks and their table, declared in startchecks.h.
 */

#include "startchecks.h"

#include <cmath>
#include <cstdio>

namespace qtmon {

std::vector<StartCheck> startCheckFailures(double clock_offset_s, TestResult test,
                                           bool approved)
{
  std::vector<StartCheck> failures;
  if (std::fabs(clock_offset_s) > kClockToleranceS)
    failures.push_back(StartCheck::Clock);
  if (test != ALL_PASSED)
    failures.push_back(StartCheck::SelfTest);
  if (!approved)
    failures.push_back(StartCheck::Approval);
  return failures;
}

std::string startCheckTable()
{
  std::string out = "clock_offset_s self_test approved | failing checks\n";
  const double offsets[] = {0.0, 1.99, -2.0, 2.01, -30.0, -1.45e9};
  const TestResult tests[] = {ALL_PASSED, TEST_UNSPECIFIED, TEST_RUNNING, RTC_FAILED};
  for (double offset : offsets) {
    for (TestResult test : tests) {
      for (bool approved : {true, false}) {
        char number[32];
        std::snprintf(number, sizeof number, "%.4g", offset);
        std::string failing;
        for (StartCheck check : startCheckFailures(offset, test, approved)) {
          if (!failing.empty())
            failing += " ";
          failing += check == StartCheck::Clock      ? "clock"
                     : check == StartCheck::SelfTest ? "self-test"
                                                     : "approval";
        }
        out += std::string(number) + " " + TestResult_Name(test) + " " +
               std::to_string(approved) + " | " + (failing.empty() ? "none" : failing) +
               "\n";
      }
    }
  }
  return out;
}

} // namespace qtmon
