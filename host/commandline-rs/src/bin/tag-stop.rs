//! `tag-stop`: stop a running tag and print the resulting status.
//!
//! Port of `host/commandline/tag-stop.cc`. Waits for the tag to report a
//! definite state after the attach reset, sends the stop, then polls until
//! the tag reaches FINISHED or ABORTED: a stop is posted and acknowledged
//! before the state machine acts on it, so an immediate status read can
//! still say RUNNING. A stop the tag accepted and has not acted on is sent
//! again; see [`Tag::stop_confirmed`].
//!
//! Exits 0 once the tag is stopped, with its status printed, including when
//! it was already FINISHED or ABORTED (no stop is then sent); 1 if the
//! attach, a status read or the stop fails, the tag never settles, is not
//! acquiring, or does not stop within `--stop-timeout`.
//!
//! Usage: `tag-stop [-d] [-b BUS:DEVICE] [--stop-timeout S]
//! [--settle-timeout S]` (defaults 30 and 10 s). Status is polled every 2 s
//! until the timeout, at least once; with `--stop-timeout 0` a tag that does
//! not stop at the first read fails. Option errors exit 2 (clap).
//!
//! # Hardware side effects
//!
//! The attach resets the tag, and the stop ends its run, leaving the stored
//! data in place for a download. A stop is sent only to a tag that is
//! acquiring (CONFIGURED, RUNNING or HIBERNATING); for any other state the
//! tool exits 1 without sending one. The status is printed even when the tag
//! fails to stop, for diagnosis.

use std::process::ExitCode;
use std::thread::sleep;
use std::time::Duration;

use clap::Parser;
use tag_tools::{BaseArgs, exit_on_interrupt, init_logging, select_base};
use tagcore::proto::{self, TagState};
use tagcore::{StopFailure, Tag};

/// stop a running tag and print the resulting status
#[derive(Parser)]
#[command(name = "tag-stop")]
struct Cli {
    /// The shared `--debug` and `--base` options.
    #[command(flatten)]
    base: BaseArgs,

    /// Seconds to wait for the tag to actually reach a stopped state. The stop
    /// request is posted and acknowledged before the state machine acts on it
    #[arg(long, default_value_t = 30, value_name = "SECONDS")]
    stop_timeout: u64,

    /// Seconds to wait after attach for the tag to report a definite state.
    /// Attach connects under reset, so the first status can legitimately be
    /// STATE_UNSPECIFIED while the tag boots
    #[arg(long, default_value_t = 10, value_name = "SECONDS")]
    settle_timeout: u64,
}

/// Prints `message` to stderr and returns exit status 1.
fn fail(message: impl std::fmt::Display) -> ExitCode {
    eprintln!("{message}");
    ExitCode::FAILURE
}

/// Writes `message` to stderr, adding a newline unless it ends with one.
fn eprint_line(message: &str) {
    if message.ends_with('\n') {
        eprint!("{message}");
    } else {
        eprintln!("{message}");
    }
}

/// Polls status a second apart until the tag reports a state other than
/// STATE_UNSPECIFIED. A booting tag refuses a stop ("Monitor request not
/// permitted in current tag state"), so the stop waits for this.
fn settle(tag: &Tag, timeout_s: u64) -> Result<(), String> {
    const SETTLE: Duration = Duration::from_secs(1);
    for _ in 0..timeout_s {
        let status = tag
            .status()
            .map_err(|_| format!("GetStatus failed: {}", tag.debug_message()))?;
        if status.state != TagState::StateUnspecified as i32 {
            return Ok(());
        }
        sleep(SETTLE);
    }
    Err(format!(
        "tag still reports STATE_UNSPECIFIED after {timeout_s} s; it is not merely settling"
    ))
}

/// Attaches, settles, then stops the tag and waits for FINISHED or ABORTED.
fn main() -> ExitCode {
    tag_tools::restore_sigpipe();
    let cli = Cli::parse();
    init_logging(&cli.base);

    let tag = Tag::new();
    let dev = select_base::<Cli>(&tag, &cli.base);
    if !tag.attach(&dev) {
        return fail("Attach failed");
    }
    exit_on_interrupt();

    if let Err(message) = settle(&tag, cli.settle_timeout) {
        return fail(message);
    }

    let status = match tag.stop_confirmed(Duration::from_secs(cli.stop_timeout)) {
        Ok(status) => status,
        Err(failure) => {
            eprintln!("{failure}");
            match &failure {
                StopFailure::NotAcquiring(status) | StopFailure::TimedOut(status) => {
                    println!("{}", proto::debug_string(&**status));
                }
                StopFailure::Refused(_) | StopFailure::Status(_) => {}
            }
            return ExitCode::FAILURE;
        }
    };

    if !status.debug_message.is_empty() {
        eprint_line(&status.debug_message);
    }
    println!("{}", proto::debug_string(&status));
    ExitCode::SUCCESS
}
