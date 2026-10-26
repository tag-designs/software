//! `tag-reset`: stop, erase, and return a tag to the idle state.
//!
//! Port of `host/commandline/tag-reset.cc`. Waits for the tag to report a
//! definite state after the attach reset, stops it if it is RUNNING or
//! HIBERNATING, erases it if it is then FINISHED or ABORTED and polls until
//! the erase reaches IDLE, and optionally syncs its clock (`--set-rtc`).
//!
//! Exits 0 with the final state printed; 1 if the attach, a status read, the
//! stop, the erase or the clock sync fails, the tag never settles, or the
//! erase does not reach IDLE within `--reset-timeout`.

use std::process::ExitCode;
use std::thread::sleep;
use std::time::Duration;

use clap::Parser;
use tag_tools::{BaseArgs, exit_on_interrupt, init_logging, select_base, state_name};
use tagcore::Tag;
use tagcore::proto::{self, Status, TagState};

/// stop, erase, and return a tag to the idle state
#[derive(Parser)]
#[command(name = "tag-reset")]
struct Cli {
    #[command(flatten)]
    base: BaseArgs,

    /// Synchronize the tag clock from the host once idle
    #[arg(long)]
    set_rtc: bool,

    /// Seconds to wait for the erase to finish
    #[arg(long, default_value_t = 180, value_name = "SECONDS")]
    reset_timeout: u64,

    /// Seconds to wait for the tag to report a definite state after attach.
    /// Attach connects under reset, so the first status can legitimately be
    /// STATE_UNSPECIFIED while the tag boots
    #[arg(long, default_value_t = 10, value_name = "SECONDS")]
    settle_timeout: u64,
}

/// Prints the firmware's debug message, if any, to stderr as is.
fn report_debug(status: &Status) {
    if !status.debug_message.is_empty() {
        eprint!("{}", status.debug_message);
    }
}

fn fail(message: impl std::fmt::Display) -> ExitCode {
    eprintln!("{message}");
    ExitCode::FAILURE
}

/// Polls status a second apart until the tag reports a state other than
/// STATE_UNSPECIFIED, which it can legitimately report while booting after
/// the attach reset. A failed read is fatal: a tag that never answered must
/// not be mistaken for one that is already idle.
fn settle(tag: &Tag, timeout_s: u64) -> Result<Status, String> {
    const SETTLE: Duration = Duration::from_secs(1);
    for _ in 0..timeout_s {
        let status = tag
            .status()
            .map_err(|_| format!("GetStatus failed: {}", tag.debug_message()))?;
        if status.state != TagState::StateUnspecified as i32 {
            return Ok(status);
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
        // Exiting 0 here would report success from a tool that never
        // reached the tag.
        return fail("Attach failed");
    }
    exit_on_interrupt();

    let mut status = match settle(&tag, cli.settle_timeout) {
        Ok(status) => status,
        Err(message) => return fail(message),
    };
    report_debug(&status);
    println!("{}", proto::debug_string(&status));

    if status.state == TagState::Running as i32 || status.state == TagState::Hibernating as i32 {
        if tag.stop().is_err() {
            return fail(format!("Stop failed: {}", tag.debug_message()));
        }
        println!("State: {}", state_name(status.state));
        status = match tag.status() {
            Ok(status) => status,
            Err(_) => {
                return fail(format!(
                    "GetStatus after stop failed: {}",
                    tag.debug_message()
                ));
            }
        };
        report_debug(&status);
    }

    println!("State: {}", state_name(status.state));
    if status.state == TagState::Finished as i32 || status.state == TagState::Aborted as i32 {
        if tag.erase().is_err() {
            return fail(format!("Erase failed: {}", tag.debug_message()));
        }

        // Erase() only posts the work; the state machine then sweeps
        // external storage in batches whose length depends on the data
        // present, so poll for IDLE rather than guess a duration.
        const POLL_MS: u64 = 250;
        let timeout_ms = cli.reset_timeout * 1000;
        let mut waited_ms = 0;
        let mut reached_idle = false;
        while waited_ms < timeout_ms {
            sleep(Duration::from_millis(POLL_MS));
            waited_ms += POLL_MS;
            let Ok(s) = tag.status() else { continue };
            status = s;
            if status.state == TagState::Idle as i32 {
                reached_idle = true;
                break;
            }
        }

        println!(
            "State: {} after {:.2} s",
            state_name(status.state),
            waited_ms as f64 / 1000.0
        );
        report_debug(&status);
        if !reached_idle {
            return fail(format!(
                "Reset did not reach IDLE within {} s; last state {}",
                cli.reset_timeout,
                state_name(status.state)
            ));
        }
    }

    if cli.set_rtc {
        if tag.set_rtc().is_ok() {
            println!("RTC synchronized");
        } else {
            // The firmware says why (e.g. NXIO "RTC sync failed while
            // writing tag clock"), which tells a failed write from a refusal.
            return fail(format!("SetRtc failed: {}", tag.debug_message()));
        }
    }

    println!("Final state: {}", state_name(status.state));
    ExitCode::SUCCESS
}
