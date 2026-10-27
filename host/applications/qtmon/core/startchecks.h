/**
 * @file    startchecks.h
 * @brief   The checks Start makes before starting a tag, which warn and let
 *          the user continue or cancel.
 *
 * @details Start is offered only when it can be done at all (see
 *          commands.h); these are the further checks a user may override.
 *          startCheckTable() prints them for every combination of inputs, and
 *          is compared with host/applications/qtmon/test/start-checks.txt.
 *          The design is in host/applications/qtmon/design/editor-and-start-checks.md.
 */

#ifndef QTMON_CORE_STARTCHECKS_H
#define QTMON_CORE_STARTCHECKS_H

#include <string>
#include <vector>

#include "tag.pb.h"

namespace qtmon {

/// Largest clock offset, in seconds, that the clock check accepts.
inline constexpr double kClockToleranceS = 2.0;

/// A start check that can fail.
enum class StartCheck {
  Clock,     ///< The tag's clock is more than kClockToleranceS off.
  SelfTest,  ///< The tag's reported self-test result is not ALL_PASSED.
  Approval,  ///< The configuration is not approved.
};

/**
 * @brief The checks that fail, in the order Start lists them.
 *
 * @param[in] clock_offset_s  Tag clock minus host clock at the latest status,
 *                            in seconds.
 * @param[in] test            The self-test result the tag reports.
 * @param[in] approved        Whether the configuration is approved.
 * @return Empty when every check passes.
 */
std::vector<StartCheck> startCheckFailures(double clock_offset_s, TestResult test,
                                           bool approved);

/**
 * @brief The sentence Start shows for a failing @p check.
 * @param[in] check           The check.
 * @param[in] clock_offset_s  The clock offset, for Clock.
 * @param[in] test            The self-test result, for SelfTest.
 * @return e.g. "Clock is -30 s off (limit 2 s).", "Self-test is RTC_FAILED.",
 *         "Configuration is not approved."
 */
std::string startCheckMessage(StartCheck check, double clock_offset_s, TestResult test);

/**
 * @brief startCheckFailures() for a fixed set of offsets, results and
 *        approvals; the text of `qtmonitor --check-start-checks`.
 * @return A header line, then one line per combination, each ending "\n".
 */
std::string startCheckTable();

} // namespace qtmon

#endif // QTMON_CORE_STARTCHECKS_H
