//! `tag-stop`: stop a running tag and print the resulting status.
//!
//! Port of `host/commandline/tag-stop.cc`. Waits for the tag to report a
//! definite state after the attach reset, sends the stop, then polls until
//! the tag reaches FINISHED or ABORTED: a stop is posted and acknowledged
//! before the state machine acts on it, so an immediate status read can
//! still say RUNNING.
//!
//! Exits 0 once the tag is stopped, with its status printed; 1 if the
//! attach, a status read or the stop fails, the tag never settles, or it
//! does not stop within `--stop-timeout`.

use std::process::ExitCode;
use std::thread::sleep;
use std::time::Duration;

use clap::Parser;
use tag_tools::{BaseArgs, exit_on_interrupt, init_logging, select_base, state_name};
use tagcore::Tag;
use tagcore::proto::{self, Status, TagState};

/// stop a running tag and print the resulting status
#[derive(Parser)]
#[command(name = "tag-stop")]
struct Cli {
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

fn main() -> ExitCode {
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

    if tag.stop().is_err() {
        let message = tag.debug_message();
        if !message.is_empty() {
            eprint_line(&message);
        }
        return fail("Stop failed");
    }

    // Poll for a terminal state, a few well-spaced tries rather than many
    // rapid ones: each poll is a request the tag must service while it is
    // doing the work being waited on.
    const POLL: Duration = Duration::from_secs(2);
    let tries = cli.stop_timeout.div_ceil(2);
    let mut status = Status::default();
    let mut stopped = false;
    for _ in 0..tries {
        status = match tag.status() {
            Ok(status) => status,
            Err(_) => {
                return fail(format!(
                    "Could not read tag status: {}",
                    tag.debug_message()
                ));
            }
        };
        if status.state == TagState::Finished as i32 || status.state == TagState::Aborted as i32 {
            stopped = true;
            break;
        }
        sleep(POLL);
    }
    if !stopped {
        eprintln!(
            "Tag did not reach a stopped state within {} s; last state {}",
            cli.stop_timeout,
            state_name(status.state)
        );
        println!("{}", proto::debug_string(&status));
        return ExitCode::FAILURE;
    }

    if !status.debug_message.is_empty() {
        eprint_line(&status.debug_message);
    }
    println!("{}", proto::debug_string(&status));
    ExitCode::SUCCESS
}
