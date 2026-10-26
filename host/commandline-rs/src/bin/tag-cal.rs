//! `tag-cal`: put an IDLE tag in sensor-calibration mode and stream its raw
//! samples until Ctrl-C.
//!
//! Port of `host/commandline/tag-calibrate.cc` (built as `tag-cal`). Syncs
//! the clock, starts calibration, then repeatedly fetches the calibration
//! log and prints each sample as `Raw:ax,ay,az,0,0,0,mx,my,mz` (CRLF line
//! ends), the format calibration front ends read. Ctrl-C ends the stream and
//! the tag is stopped.
//!
//! Exits 0 after streaming, and also when the tag is not IDLE (as the C++
//! does); 1 if the attach, the first status read, the clock sync, the
//! calibrate request or the final stop fails.

use std::process::ExitCode;
use std::sync::atomic::{AtomicBool, Ordering};

use clap::Parser;
use tag_tools::{BaseArgs, init_logging, select_base};
use tagcore::Tag;
use tagcore::proto::{TagState, ack};

/// starts calibration mode
#[derive(Parser)]
#[command(name = "tag-calibrate")]
struct Cli {
    #[command(flatten)]
    base: BaseArgs,
}

/// Cleared by Ctrl-C to end the sample stream.
static EXECUTE: AtomicBool = AtomicBool::new(true);

fn fail(message: impl std::fmt::Display) -> ExitCode {
    eprintln!("{message}");
    ExitCode::FAILURE
}

/// Streams the calibration log until Ctrl-C or a failed fetch, printing one
/// `Raw:` line per sample. Floats print with two decimals, as the C++ sets
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
        let Some(ack::Payload::CalibrationLog(log)) = reply.payload else {
            continue;
        };
        for sample in &log.data {
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
    }
}

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
        eprintln!("Tag not idle");
        return ExitCode::SUCCESS;
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
