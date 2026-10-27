//! `tag-attach-cycle`: repeatedly attach to and detach from a tag.
//!
//! Port of `host/commandline/tag-attach-cycle.cc`. Each cycle attaches
//! (which resets the tag), optionally reads and prints its status, stays
//! attached for `--on-ms`, detaches, and stays away for `--off-ms`. With
//! `--cycles 0` it runs until Ctrl-C, which ends the run between steps
//! rather than killing the process.
//!
//! Exits 0 when the cycles complete or Ctrl-C stops them; 1 if an attach
//! fails.
//!
//! Usage: `tag-attach-cycle [-d] [-b BUS:DEVICE] [--on-ms MS] [--off-ms MS]
//! [--cycles N] [--no-status]`; both intervals default to 5000 ms.
//!
//! This is a test of the attach path itself, one of the two legitimate
//! reasons to attach to a running tag: every cycle resets the tag and ends
//! whatever it was doing, so it says nothing about how a run behaves
//! unattended. A status read that fails is reported on stderr and the cycle
//! continues.
//!
//! # Hardware side effects
//!
//! One target reset per cycle. A tag past its start time is restarted by
//! each reset.

use std::process::ExitCode;
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread::sleep;
use std::time::Duration;

use clap::Parser;
use tag_tools::{BaseArgs, init_logging, select_base, state_name};
use tagcore::Tag;
use tagcore::proto::Status;

/// repeatedly attach and detach from a running tag
#[derive(Parser)]
#[command(name = "tag-attach-cycle")]
struct Cli {
    /// The shared `--debug` and `--base` options.
    #[command(flatten)]
    base: BaseArgs,

    /// Milliseconds to remain attached
    #[arg(long, default_value_t = 5000)]
    on_ms: u32,

    /// Milliseconds to remain detached
    #[arg(long, default_value_t = 5000)]
    off_ms: u32,

    /// Attach/detach cycle count; 0 runs until Ctrl-C
    #[arg(long, default_value_t = 0)]
    cycles: u32,

    /// Do not read tag status after attaching
    #[arg(long)]
    no_status: bool,
}

/// Set by Ctrl-C to end the cycling.
static STOP_REQUESTED: AtomicBool = AtomicBool::new(false);

/// Sleeps for `ms`, in steps of at most 100 ms, returning early on Ctrl-C.
fn interruptible_sleep(ms: u32) {
    let mut remaining = u64::from(ms);
    while !STOP_REQUESTED.load(Ordering::SeqCst) && remaining > 0 {
        let nap = remaining.min(100);
        sleep(Duration::from_millis(nap));
        remaining -= nap;
    }
}

/// Prints the tag's state, clock (ms since the Unix epoch) and internal and
/// external flash page counts for `cycle`, and any firmware debug message on
/// stderr.
fn print_status(cycle: u32, status: &Status) {
    println!(
        "cycle {cycle}: state={} millis={} internal={} external={}",
        state_name(status.state),
        status.millis,
        status.internal_data_count,
        status.external_data_count
    );
    if !status.debug_message.is_empty() {
        eprint!("{}", status.debug_message);
    }
}

/// Installs the Ctrl-C handler, then runs the attach/detach cycles.
fn main() -> ExitCode {
    let _ = ctrlc::set_handler(|| STOP_REQUESTED.store(true, Ordering::SeqCst));
    let cli = Cli::parse();
    init_logging(&cli.base);

    let tag = Tag::new();
    let dev = select_base::<Cli>(&tag, &cli.base);

    let mut cycle: u32 = 1;
    while !STOP_REQUESTED.load(Ordering::SeqCst) && (cli.cycles == 0 || cycle <= cli.cycles) {
        println!("cycle {cycle}: attach");
        if !tag.attach(&dev) {
            eprintln!("cycle {cycle}: attach failed");
            return ExitCode::FAILURE;
        }

        if !cli.no_status {
            match tag.status() {
                Ok(status) => print_status(cycle, &status),
                Err(_) => eprintln!("cycle {cycle}: status failed: {}", tag.debug_message()),
            }
        }

        interruptible_sleep(cli.on_ms);

        println!("cycle {cycle}: detach");
        tag.detach();

        if cli.cycles == 0 || cycle < cli.cycles {
            interruptible_sleep(cli.off_ms);
        }
        cycle = cycle.wrapping_add(1);
    }

    tag.detach();
    ExitCode::SUCCESS
}
