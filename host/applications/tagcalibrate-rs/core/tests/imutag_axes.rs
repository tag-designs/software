//! Replays the IMUTagNandBmp581 capture through the engine and requires the
//! dip it ends on to be this site's, with a small spread: the regression
//! check that the IMUTag's magnetometer and accelerometer still meet the
//! axis contract of `docs/shared/sensor-axes.md`.
//!
//! The capture is from firmware that maps the BMM350 into the host's frame
//! itself (`IMUTAG_MAG_TAG_*`); `check_orientation_frames.py` scores it at
//! 63.71 degrees with 2.86 degrees of spread, and the same sweep from
//! firmware built before the mapping read -33.8 +/- 34.0 in the engine. A
//! change on either side -- the firmware's mapping or the host's eCompass
//! convention -- that breaks the pairing moves the spread by tens of degrees.
//!
//! Kept apart from `replay_log.rs` because the engine's logger is
//! process-wide, and each test file is its own process.

use std::sync::mpsc;
use std::time::Duration;

use serde_json::json;
use tagcalibrate_core::engine::{self, EngineConfig, Event};

/// The IMUTag capture.
const FIXTURE: &str = "../../../docs/fixtures/qtcalibrate/qtcalibrate-samples-20261028-171428.json";

#[test]
fn imutag_capture_gives_the_sites_dip() {
    let fixture = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join(FIXTURE);
    let config = EngineConfig {
        home_dir: std::env::temp_dir(),
        user_guide_url: None,
        auto_attach: false,
        replay: Some(fixture),
        replay_period: Duration::from_millis(1),
    };
    let _ = log::set_logger(&engine::LOGGER);
    let (tx, rx) = mpsc::channel();
    let engine = engine::spawn(config, move |event| {
        if let Event::Log { text, .. } = event {
            let _ = tx.send(text);
        }
    });
    engine.command("set_log_level", json!({ "level": "DEBUG" }));
    engine.command("ready", json!({}));
    engine.command("set_stream", json!({ "on": true }));
    engine.command("start", json!({}));

    let mut last = String::new();
    loop {
        let text = rx
            .recv_timeout(Duration::from_secs(60))
            .expect("replay finished");
        if text == "replay complete" {
            break;
        }
        if text.starts_with("magquality:") {
            last = text;
        }
    }

    // "... dip 63.36 +/- 2.52 deg (p95 ...": the median dip and its spread.
    let dip = last.split(" dip ").nth(1).expect("dip figures");
    let mut words = dip.split_whitespace();
    let mean: f64 = words.next().and_then(|w| w.parse().ok()).expect("dip");
    let spread: f64 = words.nth(1).and_then(|w| w.parse().ok()).expect("spread");
    assert!((62.0..65.0).contains(&mean), "dip {mean} ({last})");
    assert!(spread < 5.0, "dip spread {spread} ({last})");
}
