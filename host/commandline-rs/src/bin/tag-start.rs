//! `tag-start`: program a configuration into an IDLE tag and start it.
//!
//! Port of `host/commandline/tag-start.cc`. Waits for the tag to report a
//! definite state, optionally syncs its clock (`--set-rtc`), then starts from
//! the configuration the tag already holds. `--config` replaces it with a
//! JSON file, or with `--merge` overlays the file using
//! [`ConfigShape::merge`]: only fields the tag supports and the file speaks
//! for are copied, zeros included. `--start-now` forces `start_delay` to 0.
//! It then polls until the tag leaves IDLE and prints the resulting status,
//! since an acknowledged start is not proof the tag started.
//!
//! Exits 0 when the tag reached RUNNING or CONFIGURED, was already in one of
//! them (no start is sent), or accepted the start but answered no status read
//! within `--start-timeout` (reported as "not confirmed"); 1 otherwise.
//!
//! Usage: `tag-start [-d] [-b BUS:DEVICE] [-c|--config FILE [--merge]]
//! [--set-rtc] [--start-now] [--start-timeout S] [--settle-timeout S]`
//! (both timeouts default to 10 s; status is polled once a second). The
//! configuration actually sent is printed as protobuf text before the
//! start. A non-IDLE tag other than RUNNING or CONFIGURED is refused with
//! exit 1, as is `--set-rtc` on a tag that is not IDLE. Option errors exit 2
//! (clap).
//!
//! # Hardware side effects
//!
//! The attach resets the tag. `--set-rtc` overwrites its clock with the
//! host's. The start writes the configuration to the tag and begins a run
//! (CONFIGURED until `start_delay` elapses, then RUNNING), which the tool
//! does not end: the tag is left running when it exits. After an accepted
//! start, library link errors are silenced unless `--debug`, because a tag
//! whose next state sleeps at once drops the debug link.
//!
//! This does not erase a previous log: a tag holding data is FINISHED or
//! ABORTED, not IDLE, and must be reset with `tag-reset` first.

use std::process::ExitCode;
use std::thread::sleep;
use std::time::Duration;

use clap::Parser;
use log::LevelFilter;
use tag_tools::{
    BaseArgs, exit_on_interrupt, init_logging, select_base, set_log_level, state_name,
};
use tagcore::Tag;
use tagcore::configshape::ConfigShape;
use tagcore::proto::{self, Config, Status, TagState, TagType};

/// start a configured tag and print the resulting status
#[derive(Parser)]
#[command(name = "tag-start")]
struct Cli {
    /// The shared `--debug` and `--base` options.
    #[command(flatten)]
    base: BaseArgs,

    /// Program this protobuf-JSON configuration before starting
    #[arg(short, long, value_name = "FILE")]
    config: Option<String>,

    /// Overlay --config onto the tag's stored configuration rather than
    /// replacing it: only fields the tag supports are copied
    #[arg(long)]
    merge: bool,

    /// Synchronize the tag clock from the host before starting
    #[arg(long)]
    set_rtc: bool,

    /// Force start_delay to zero so collection begins immediately
    #[arg(long)]
    start_now: bool,

    /// Seconds to wait for the tag to leave IDLE after the start is accepted.
    /// Start is posted, not performed: the state machine acts on it later, so
    /// a status read straight afterwards can still say IDLE
    #[arg(long, default_value_t = 10, value_name = "SECONDS")]
    start_timeout: u64,

    /// Seconds to wait after attach for the tag to report a definite state.
    /// Attach connects under reset, so the first status can legitimately be
    /// STATE_UNSPECIFIED while the tag boots
    #[arg(long, default_value_t = 10, value_name = "SECONDS")]
    settle_timeout: u64,
}

/// Interval between status polls while settling and while waiting for the
/// tag to leave IDLE.
const POLL: Duration = Duration::from_secs(1);

/// Loads a Config from a protobuf-JSON file: replacing `cfg`, or with
/// `merge` overlaying only the fields the tag supports and the file speaks
/// for ([`ConfigShape::for_file`], so a stated zero is copied).
fn load_config_json(path: &str, cfg: &mut Config, merge: bool) -> Result<(), String> {
    let text =
        std::fs::read_to_string(path).map_err(|_| format!("Cannot open config file: {path}"))?;
    let parsed: Config =
        proto::from_json(&text).map_err(|e| format!("Config JSON did not parse: {e}"))?;
    if merge {
        let file_shape =
            ConfigShape::for_file(&text).map_err(|e| format!("Config JSON did not parse: {e}"))?;
        let tag_type = TagType::try_from(cfg.tag_type).unwrap_or(TagType::TagUnspecified);
        ConfigShape::for_tag(tag_type).merge(cfg, &parsed, &file_shape);
    } else {
        *cfg = parsed;
    }
    Ok(())
}

/// Polls status until the tag reports a state other than STATE_UNSPECIFIED,
/// which it can legitimately report while booting after the attach reset.
fn settle(tag: &Tag, timeout_s: u64) -> Result<Status, String> {
    let tries = timeout_s.max(1);
    for _ in 0..tries {
        let status = tag
            .status()
            .map_err(|_| format!("GetStatus failed: {}", tag.debug_message()))?;
        if status.state != TagState::StateUnspecified as i32 {
            return Ok(status);
        }
        sleep(POLL);
    }
    Err(format!(
        "tag still reports STATE_UNSPECIFIED after {timeout_s} s; it is not merely settling"
    ))
}

/// Prints `message` to stderr and returns exit status 1.
fn fail(message: impl std::fmt::Display) -> ExitCode {
    eprintln!("{message}");
    ExitCode::FAILURE
}

/// Attaches, settles, optionally sets the clock, starts an IDLE tag with the
/// chosen configuration and polls until it leaves IDLE.
fn main() -> ExitCode {
    let cli = Cli::parse();
    init_logging(&cli.base);

    let tag = Tag::new();
    let dev = select_base::<Cli>(&tag, &cli.base);
    if !tag.attach(&dev) {
        return fail("Attach failed");
    }
    exit_on_interrupt();

    // Read status before syncing the clock: the firmware accepts set_rtc
    // only in IDLE, and rejects one sent as the first request after attach.
    let mut status = match settle(&tag, cli.settle_timeout) {
        Ok(status) => status,
        Err(message) => return fail(message),
    };
    let idle = TagState::Idle as i32;

    if cli.set_rtc {
        if status.state != idle {
            return fail(format!(
                "SetRtc skipped: tag is {}, the tag accepts a clock sync only when IDLE",
                state_name(status.state)
            ));
        }
        if tag.set_rtc().is_err() {
            let why = tag.debug_message();
            return fail(if why.is_empty() {
                "SetRtc failed".to_owned()
            } else {
                format!("SetRtc failed: {why}")
            });
        }
        println!("RTC synchronized");
    }

    let mut start_attempted = false;
    let mut start_failed = false;
    if status.state == idle {
        // Starting from a default config would run settings nobody chose.
        let mut cfg = match tag.config() {
            Ok(cfg) => cfg,
            Err(_) => return fail(format!("GetConfig failed: {}", tag.debug_message())),
        };
        if let Some(path) = &cli.config
            && let Err(message) = load_config_json(path, &mut cfg, cli.merge)
        {
            return fail(message);
        }
        if cli.start_now {
            cfg.start_delay = 0;
        }
        print!(
            "Starting with configuration:\n{}",
            proto::debug_string(&cfg)
        );
        start_attempted = true;
        if tag.start(&cfg).is_ok() {
            // From here the tag may leave the debug link at any moment (a tag
            // whose next state sleeps at once must), so the link errors
            // tagcore would log are expected; keep them unless --debug.
            if !cli.base.debug {
                set_log_level(LevelFilter::Off);
            }
        } else {
            start_failed = true;
            let message = tag.debug_message();
            if message.is_empty() {
                println!("Start failed");
            } else {
                println!("Start failed: {message}");
            }
        }
    } else if status.state == TagState::Running as i32
        || status.state == TagState::Configured as i32
    {
        println!("Start skipped: tag is already {}", state_name(status.state));
    } else {
        // A skipped start is not a success for a tool whose job is to start.
        eprintln!(
            "Start skipped: tag is {}; a start is accepted only in IDLE",
            state_name(status.state)
        );
        println!("State: {}", state_name(status.state));
        return ExitCode::FAILURE;
    }

    if start_attempted && start_failed {
        // The tag refused the start ("Start failed" above).
        println!("Last known state: {}", state_name(status.state));
        return ExitCode::FAILURE;
    }

    // Start() returns when the request is accepted; the state machine acts
    // on it later, so poll until the tag has left IDLE. A failed read is not
    // a reason to stop: it is a tag that did not answer in time, which is
    // what the remaining tries are for.
    let tries = if start_attempted {
        cli.start_timeout.max(1)
    } else {
        1
    };
    let mut read_ok = false;
    for _ in 0..tries {
        match tag.status() {
            Ok(s) => {
                status = s;
                read_ok = true;
                if !start_attempted || status.state != idle {
                    break;
                }
            }
            // TODO(doc-review): a failed read after an earlier successful one that still
            // showed IDLE clears `read_ok`, so the tool reports "no status read succeeded"
            // and exits 0 although the last good read said the tag had not started.
            Err(_) => read_ok = false,
        }
        sleep(POLL);
    }

    if !read_ok && start_attempted {
        println!(
            "Start accepted, but no status read succeeded in {} s, so the tag's new state was \
             not confirmed",
            cli.start_timeout
        );
        println!(
            "State: not confirmed (last read: {})",
            state_name(status.state)
        );
        return ExitCode::SUCCESS;
    }
    if !read_ok {
        eprintln!("GetStatus failed: {}", tag.debug_message());
        println!("Last known state: {}", state_name(status.state));
        return ExitCode::FAILURE;
    }
    if start_attempted && status.state == idle {
        eprintln!(
            "Tag did not leave IDLE within {} s of the start being accepted",
            cli.start_timeout
        );
        println!("State: {}", state_name(status.state));
        return ExitCode::FAILURE;
    }
    println!("State: {}", state_name(status.state));
    // Leaving IDLE is not the same as having started: a start that fails in
    // device setup goes IDLE -> ABORTED.
    if start_attempted
        && status.state != TagState::Running as i32
        && status.state != TagState::Configured as i32
    {
        return fail(format!(
            "Start did not lead to RUNNING or CONFIGURED: {}",
            state_name(status.state)
        ));
    }
    ExitCode::SUCCESS
}
