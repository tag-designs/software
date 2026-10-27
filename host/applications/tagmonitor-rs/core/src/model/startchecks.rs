//! The checks Start makes before starting a tag, which warn and let the
//! user continue or cancel.
//!
//! Port of `host/applications/qtmon/core/startchecks.h`. [`start_check_table`]
//! is the text of `qtmonitor --check-start-checks`, compared with
//! `host/applications/qtmon/test/start-checks.txt`.

use tagcore::proto::TestResult;
use tagcore::textformat::format_g;

/// Largest clock offset, in seconds, that the clock check accepts.
pub const CLOCK_TOLERANCE_S: f64 = 2.0;

/// A start check that can fail.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StartCheck {
    /// The tag's clock is more than [`CLOCK_TOLERANCE_S`] off.
    Clock,
    /// The tag's reported self-test result is not ALL_PASSED.
    SelfTest,
    /// The configuration is not approved.
    Approval,
}

/// C's `%.<precision>g`, which the C++ uses for these numbers.
fn printf_g(value: f64, precision: usize) -> String {
    format_g(value, precision, precision, |_| true)
}

/// The checks that fail, in the order Start lists them; empty when every
/// check passes.
///
/// `clock_offset_s` is the tag clock minus the host clock at the latest
/// status, `test` the self-test result the tag reports, `approved` whether
/// the configuration is approved.
pub fn start_check_failures(
    clock_offset_s: f64,
    test: TestResult,
    approved: bool,
) -> Vec<StartCheck> {
    let mut failures = Vec::new();
    if clock_offset_s.abs() > CLOCK_TOLERANCE_S {
        failures.push(StartCheck::Clock);
    }
    if test != TestResult::AllPassed {
        failures.push(StartCheck::SelfTest);
    }
    if !approved {
        failures.push(StartCheck::Approval);
    }
    failures
}

/// The sentence Start shows for a failing `check`: `"Clock is -30 s off
/// (limit 2 s)."`, `"Self-test is RTC_FAILED."`, `"Configuration is not
/// approved."`.
pub fn start_check_message(check: StartCheck, clock_offset_s: f64, test: TestResult) -> String {
    match check {
        StartCheck::Clock => format!(
            "Clock is {} s off (limit {} s).",
            printf_g(clock_offset_s, 3),
            printf_g(CLOCK_TOLERANCE_S, 6)
        ),
        StartCheck::SelfTest => format!("Self-test is {}.", test.as_str_name()),
        StartCheck::Approval => "Configuration is not approved.".to_string(),
    }
}

/// [`start_check_failures`] for a fixed set of offsets, results and
/// approvals; the text of `qtmonitor --check-start-checks`: a header line,
/// then one line per combination, each ending `"\n"`.
pub fn start_check_table() -> String {
    let mut out = String::from("clock_offset_s self_test approved | failing checks\n");
    let offsets = [0.0, 1.99, -2.0, 2.01, -30.0, -1.45e9];
    let tests = [
        TestResult::AllPassed,
        TestResult::TestUnspecified,
        TestResult::TestRunning,
        TestResult::RtcFailed,
    ];
    for offset in offsets {
        for test in tests {
            for approved in [true, false] {
                let failing: Vec<&str> = start_check_failures(offset, test, approved)
                    .into_iter()
                    .map(|c| match c {
                        StartCheck::Clock => "clock",
                        StartCheck::SelfTest => "self-test",
                        StartCheck::Approval => "approval",
                    })
                    .collect();
                out += &format!(
                    "{} {} {} | {}\n",
                    printf_g(offset, 4),
                    test.as_str_name(),
                    u8::from(approved),
                    if failing.is_empty() {
                        "none".to_string()
                    } else {
                        failing.join(" ")
                    }
                );
            }
        }
    }
    out
}
