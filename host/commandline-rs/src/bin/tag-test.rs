//! `tag-test`: set the RTC and execute tag self-tests.
//!
//! Port of `host/commandline/test.cc`. Prints the tag's information, its
//! clock drift before and after an RTC sync, its state and state log, then,
//! if the tag is IDLE, runs the requested self-test (`--test`, by TestReq
//! name or number) and polls until the tag returns to IDLE or
//! `--test-timeout` expires, and prints the test result. The RTC is synced
//! again afterwards, since a test may alter it.
//!
//! Usage: `tag-test [-d] [-b BUS:DEVICE] [-t|--test NAME|NUMBER]
//! [--test-timeout S]` (defaults `RUN_ALL` and 30 s). Status is polled once
//! a second while the test runs. Clock drift prints in seconds (tag minus
//! host), voltage in volts.
//!
//! # Exit status
//!
//! 0 once a result has been printed and the clock re-synced, **whatever the
//! result**: a `TestResult` other than a pass still exits 0, so a script must
//! read the `Test Result:` line. 1 for an unknown test, a failed attach,
//! info or status read or clock sync, a tag that is not IDLE, a refused test
//! request, or a test that does not finish within `--test-timeout`. Option
//! errors exit 2 (clap).
//!
//! # Hardware side effects
//!
//! The attach resets the tag, its clock is set from the host twice, and the
//! self-test exercises the hardware. On a tag with external NAND, the
//! external-flash test can write the NAND bad-block map. Run it after every
//! flash, before measuring anything (step 4 of the flashing protocol in
//! `AGENTS.md`).

use std::process::ExitCode;
use std::thread::sleep;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use clap::Parser;
use tag_tools::{BaseArgs, enum_name, exit_on_interrupt, init_logging, select_base, state_name};
use tagcore::Tag;
use tagcore::proto::{TagState, TestReq, TestResult, state};

/// sets the RTC and executes tag self-tests
#[derive(Parser)]
#[command(name = "tag-test")]
struct Cli {
    /// The shared `--debug` and `--base` options.
    #[command(flatten)]
    base: BaseArgs,

    /// Self-test to run by TestReq name or number
    #[arg(short, long, default_value = "RUN_ALL")]
    test: String,

    /// Seconds to wait for the tag to return to IDLE before giving up. A test
    /// that has not finished has no result to report, so this is a give-up
    /// time, not a pass/fail threshold. Raise it for slower tests or tags
    #[arg(long, default_value_t = 30, value_name = "SECONDS")]
    test_timeout: i32,
}

/// Parses an integer as `std::stoi(s, &parsed, 0)` does (leading
/// whitespace, optional sign, `0x` hex or leading-`0` octal), requiring the
/// whole string to be consumed and the value to fit an `int`.
fn parse_c_int(text: &str) -> Option<i32> {
    let s = text.trim_start();
    let (negative, s) = match s.as_bytes().first() {
        Some(b'-') => (true, &s[1..]),
        Some(b'+') => (false, &s[1..]),
        _ => (false, s),
    };
    let (radix, digits) = if let Some(hex) = s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")) {
        (16, hex)
    } else if s.len() > 1 && s.starts_with('0') {
        (8, &s[1..])
    } else {
        (10, s)
    };
    if digits.is_empty() || !digits.chars().all(|c| c.is_digit(radix)) {
        return None;
    }
    let magnitude = i64::from_str_radix(digits, radix).ok()?;
    i32::try_from(if negative { -magnitude } else { magnitude }).ok()
}

/// A TestReq by protobuf name, or by a number that names a valid value.
fn parse_test_req(name: &str) -> Option<TestReq> {
    TestReq::from_str_name(name)
        .or_else(|| parse_c_int(name).and_then(|v| TestReq::try_from(v).ok()))
}

/// The tag clock's offset from the host clock in seconds (tag minus host),
/// echoing any firmware debug message to stderr. `None` if status failed.
fn rtc_drift(tag: &Tag) -> Option<f32> {
    let status = tag.status().ok()?;
    if !status.debug_message.is_empty() {
        eprint!("{}", status.debug_message);
    }
    let now_ms = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as i64;
    Some(((status.millis - now_ms) as f64 / 1000.0) as f32)
}

/// Attaches, prints the tag's information and clock drift, sets the clock,
/// prints the state log, runs the self-test and reports its result.
fn main() -> ExitCode {
    let cli = Cli::parse();
    init_logging(&cli.base);

    let tag = Tag::new();
    let dev = select_base::<Cli>(&tag, &cli.base);
    let Some(requested_test) = parse_test_req(&cli.test) else {
        eprintln!("Invalid test: {}", cli.test);
        return ExitCode::FAILURE;
    };
    if !tag.attach(&dev) {
        eprintln!("Attach failed");
        return ExitCode::FAILURE;
    }
    exit_on_interrupt();

    let info = match tag.tag_info() {
        Ok(info) => info,
        Err(_) => {
            eprintln!("GetTagInfo failed: {}", tag.debug_message());
            return ExitCode::FAILURE;
        }
    };

    // The C++ sets std::fixed with precision 2 for floats; tag_type prints
    // as its number.
    println!("Tag Type: {}", info.tag_type);
    println!("Board name: {}", info.board_desc);
    println!("Build time and date: {}", info.build_time);
    println!("Internal Flash size: {}kb", info.intflashsz);
    println!("External Flash size: {}kb", info.extflashsz);
    println!("Repo: {}", info.gitrepo);
    println!("Repo hash: {}", info.githash);
    println!("UUID: {}", info.uuid);
    if let Some(v) = tag.voltage() {
        println!("Voltage: {v:.2}");
    }

    if let Some(f) = rtc_drift(&tag) {
        println!("Initial Clock drift: {f:.2}");
    }
    println!("#  Checking RTC (2 second delay)");
    if tag.set_rtc().is_err() {
        eprintln!("SetRtc failed: {}", tag.debug_message());
        return ExitCode::FAILURE;
    }
    sleep(Duration::from_millis(2000));
    if let Some(f) = rtc_drift(&tag) {
        println!("Clock drift after setting: {f:.2}");
    }

    let mut status = match tag.status() {
        Ok(status) => status,
        Err(_) => {
            eprintln!("GetStatus failed: {}", tag.debug_message());
            return ExitCode::FAILURE;
        }
    };
    if !status.debug_message.is_empty() {
        eprint!("{}", status.debug_message);
    }
    println!("State: {}", state_name(status.state));

    let mut next = 0;
    while let Ok(log) = tag.state_log(next) {
        if log.states.is_empty() {
            break;
        }
        next += log.states.len() as i32;
        for marker in &log.states {
            let s = marker.status.clone().unwrap_or_default();
            println!("State: {}", state_name(s.state));
            println!(
                "{}",
                enum_name(marker.transition_reason, state::Event::as_str_name)
            );
        }
    }

    if status.state != TagState::Idle as i32 {
        // No test ran, so there is no result.
        println!("#  State not idle ! Exiting");
        return ExitCode::FAILURE;
    }

    println!("#  Running test {}", requested_test.as_str_name());
    // A result is only meaningful if the request was accepted and the
    // status reporting it was actually read.
    if tag.test(requested_test).is_err() {
        eprintln!("Test request failed: {}", tag.debug_message());
        return ExitCode::FAILURE;
    }
    // The tag returns to IDLE when the test is finished; until then
    // test_status is TEST_RUNNING, a state rather than a verdict.
    let test_poll_s = cli.test_timeout;
    let mut status_read = false;
    let mut completed = false;
    for _ in 0..test_poll_s.max(0) {
        sleep(Duration::from_millis(1000));
        match tag.status() {
            Ok(s) => status = s,
            Err(_) => {
                eprintln!("GetStatus failed: {}", tag.debug_message());
                continue;
            }
        }
        status_read = true;
        if !status.debug_message.is_empty() {
            eprint!("{}", status.debug_message);
        }
        if status.state == TagState::Idle as i32 {
            completed = true;
            break;
        }
    }
    if !status_read {
        eprintln!("No status was read after the test; not reporting a result");
        return ExitCode::FAILURE;
    }
    if !completed {
        eprintln!(
            "Test did not finish within {test_poll_s} s; last state {}, no result to report. Use \
             --test-timeout to allow longer.",
            state_name(status.state)
        );
        return ExitCode::FAILURE;
    }
    println!(
        "Test Result: {}",
        enum_name(status.test_status, TestResult::as_str_name)
    );
    if tag.set_rtc().is_err() {
        // reset rtc since test may alter
        eprintln!("SetRtc after test failed: {}", tag.debug_message());
        return ExitCode::FAILURE;
    }
    ExitCode::SUCCESS
}

#[cfg(test)]
/// Unit tests for self-test selection.
mod tests {
    use super::*;

    #[test]
    /// Checks names, decimal, hex and octal numbers, and rejects numbers that
    /// name no test or carry trailing text.
    fn test_req_parses_names_and_numbers() {
        assert_eq!(parse_test_req("RUN_ALL"), Some(TestReq::RunAll));
        assert_eq!(parse_test_req("2"), Some(TestReq::RunRtc));
        assert_eq!(parse_test_req("0x0e"), Some(TestReq::RunMx25u12843));
        assert_eq!(parse_test_req("016"), Some(TestReq::RunMx25u12843));
        assert_eq!(parse_test_req("9"), None);
        assert_eq!(parse_test_req("2x"), None);
        assert_eq!(parse_test_req("bogus"), None);
        assert_eq!(parse_test_req("0x-2"), None);
    }
}
