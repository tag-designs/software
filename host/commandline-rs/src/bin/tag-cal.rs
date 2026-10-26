//! `tag-cal`: put an IDLE tag in sensor-calibration mode and stream its raw
//! samples until Ctrl-C.
//!
//! Port of `host/commandline/tag-calibrate.cc` (built as `tag-cal`). Syncs
//! the clock, starts calibration, then repeatedly fetches the calibration
//! log and prints each sample as `Raw:ax,ay,az,0,0,0,mx,my,mz` (CRLF line
//! ends), the format calibration front ends read. Ctrl-C ends the stream and
//! the tag is stopped.
//!
//! Exits 0 after streaming; 1 if the tag is not IDLE, or the attach, the
//! first status read, the clock sync, the calibrate request or the final
//! stop fails.
//!
//! Usage: `tag-cal [-d] [-b BUS:DEVICE]`. The clap command name is
//! `tag-calibrate`, the C++ name, so help and errors print that.
//!
//! # Hardware side effects
//!
//! The attach resets the tag; `SetRtc` sets its clock from the host;
//! `Calibrate` moves it into calibration mode, and `Stop` takes it out on
//! the way out. The Ctrl-C handler is installed only after the attach, so a
//! Ctrl-C during the attach kills the process; one after it, even before the
//! stream starts, still ends with the stop. A tag left in calibration mode
//! (a killed process or a failed stop) must be stopped with `tag-stop`. A
//! tag family without a calibration mode refuses (BitTag, for example).

use std::process::ExitCode;
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread::sleep;
use std::time::Duration;

use clap::Parser;
use tag_tools::{BaseArgs, init_logging, select_base, state_name};
use tagcore::Tag;
use tagcore::proto::{TagState, ack};

/// starts calibration mode
#[derive(Parser)]
#[command(name = "tag-calibrate")]
struct Cli {
    /// The shared `--debug` and `--base` options.
    #[command(flatten)]
    base: BaseArgs,
}

/// Cleared by Ctrl-C to end the sample stream.
static EXECUTE: AtomicBool = AtomicBool::new(true);

/// Prints `message` to stderr and returns exit status 1.
fn fail(message: impl std::fmt::Display) -> ExitCode {
    eprintln!("{message}");
    ExitCode::FAILURE
}

/// Streams the calibration log until Ctrl-C or a failed fetch, printing one
/// `Raw:` line per sample. The firmware buffers samples between requests, so
/// requests are 100 ms apart, qtcalibrate's streaming interval. Floats print with two decimals, as the C++ sets
/// `std::fixed` with precision 2; a missing sensor prints as zeros.
fn stream_samples(tag: &Tag) {
    while EXECUTE.load(Ordering::SeqCst) {
        let reply = match tag.calibration_log() {
            Ok(reply) => reply,
            Err(_) => {
                eprintln!("GetCalibrationLog failed: {}", tag.debug_message());
                break;
            }
        };
        let samples = match reply.payload {
            Some(ack::Payload::CalibrationLog(log)) => log.data,
            _ => Vec::new(),
        };
        for sample in &samples {
            let mut line = String::from("Raw:");
            match &sample.accel {
                Some(a) => line.push_str(&format!("{:.2},{:.2},{:.2},", a.ax, a.ay, a.az)),
                None => line.push_str("0,0,0,"),
            }
            line.push_str("0,0,0,");
            match &sample.mag {
                Some(m) => line.push_str(&format!("{:.2},{:.2},{:.2}", m.mx, m.my, m.mz)),
                None => line.push_str("0,0,0"),
            }
            print!("{line}\r\n");
        }
        sleep(Duration::from_millis(100));
    }
}

/// Attaches, checks the tag is IDLE, syncs its clock, starts calibration,
/// streams until Ctrl-C and stops the tag.
fn main() -> ExitCode {
    let cli = Cli::parse();
    init_logging(&cli.base);

    let tag = Tag::new();
    let dev = select_base::<Cli>(&tag, &cli.base);
    if !tag.attach(&dev) {
        return fail("Attach failed");
    }
    // Ctrl-C ends the stream so the tag is stopped, rather than exiting.
    let _ = ctrlc::set_handler(|| EXECUTE.store(false, Ordering::SeqCst));

    // A discarded failure would report "Tag not idle" for a tag that never
    // answered.
    let status = match tag.status() {
        Ok(status) => status,
        Err(_) => return fail(format!("GetStatus failed: {}", tag.debug_message())),
    };
    if status.state != TagState::Idle as i32 {
        return fail(format!("Tag not idle: {}", state_name(status.state)));
    }

    if tag.set_rtc().is_err() {
        return fail(format!("SetRtc failed: {}", tag.debug_message()));
    }
    if tag.calibrate().is_err() {
        return fail(format!("Calibrate failed: {}", tag.debug_message()));
    }
    stream_samples(&tag);
    if tag.stop().is_err() {
        return fail(format!("Stop failed: {}", tag.debug_message()));
    }
    ExitCode::SUCCESS
}
