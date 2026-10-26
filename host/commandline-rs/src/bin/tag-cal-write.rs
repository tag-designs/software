//! `tag-cal-write`: write an identity (no-op) magnetometer calibration.
//!
//! Port of `host/commandline/tag-cal-write.cc`. Writes `M' = A(M - V)` with
//! A = identity and V = 0, and a zero accelerometer offset. Several
//! CompassTag-family state handlers refuse to start a run until at least one
//! calibration entry is stored ("Device must be calibrated"), which a
//! mass-erase reflash wipes; this unblocks power and lifecycle testing on a
//! freshly reflashed board. Readings taken under it are NOT calibrated: run
//! tag-cal or qtcalibrate before trusting compass output from the board.

use std::process::ExitCode;

use clap::Parser;
use tag_tools::{BaseArgs, init_logging, select_base};
use tagcore::Tag;
use tagcore::proto::{CalibrationConstants, calibration_constants};

/// write an identity (no-op) calibration constant set, to unblock a tag that
/// refuses to start with "Device must be calibrated" after a mass-erase
/// reflash -- NOT a substitute for real calibration
#[derive(Parser)]
#[command(name = "tag-cal-write")]
struct Cli {
    #[command(flatten)]
    base: BaseArgs,
}

/// Identity magnetometer constants and a zero accelerometer offset.
fn identity_constants() -> CalibrationConstants {
    CalibrationConstants {
        timestamp: 0,
        magnetometer: Some(calibration_constants::MagConstants {
            a00: 1.0,
            a11: 1.0,
            a22: 1.0,
            ..Default::default()
        }),
        accelerometer: Some(calibration_constants::AccelConstants::default()),
    }
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    init_logging(&cli.base);

    let tag = Tag::new();
    let dev = select_base::<Cli>(&tag, &cli.base);
    if !tag.attach(&dev) {
        eprintln!("Attach failed");
        return ExitCode::FAILURE;
    }

    if tag.write_calibration(&identity_constants()).is_err() {
        eprintln!("WriteCalibration failed: {}", tag.debug_message());
        return ExitCode::FAILURE;
    }
    println!(
        "wrote identity calibration constants (placeholder only -- not a real calibration; \
         compass output will not be accurate until tag-cal/qtcalibrate is run)"
    );
    ExitCode::SUCCESS
}
