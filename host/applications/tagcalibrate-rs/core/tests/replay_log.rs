//! Replays qtcalibrate's reference capture through the engine, headless, and
//! compares the quality lines it logs with qtcalibrate's for the same replay
//! (`qtcalibrate --replay-capture ... --replay-exit --log-file`).
//!
//! qtcalibrate logs four `DEBUG` lines on every quality update; the engine
//! logs the same, in the same format. Set `QTCALIBRATE_LOG` to a qtcalibrate
//! log of this capture to compare every line; without it the test checks the
//! last update's four lines, which are written out below.

use std::sync::mpsc;
use std::time::Duration;

use serde_json::json;
use tagcalibrate_core::engine::{self, EngineConfig, Event};

/// The reference capture.
const FIXTURE: &str = "../../../docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-145438.json";

/// qtcalibrate's last quality update for that capture.
const LAST: [&str; 4] = [
    "magquality: coverage 90/100 isotropy 0.722 attitude 2.06 (185 cells over 90 patches) dip 63.54 +/- 2.70 deg (p95 5.99, 559 of 650 in gate)",
    "residual: field 46.14 uT fit 0.43% spread 0.43% p95 0.94% hard iron 0.02 uT (650 samples)",
    "gravity: offset -31.84 -0.80 -74.50 = 81.02 mg radius 994.1 residual 34.96 (691 held over 32/32 patches, isotropy 0.910, 775 offered 84 gated)",
    "retention: 125 evictions (105 by leverage, 20 as outliers)",
];

/// Whether a log line is one of the quality update's four.
fn quality_line(text: &str) -> bool {
    ["magquality:", "residual:", "gravity:", "retention:"]
        .iter()
        .any(|p| text.starts_with(p))
}

#[test]
fn replay_logs_what_qtcalibrate_logs() {
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

    let mut lines = Vec::new();
    loop {
        let text = rx
            .recv_timeout(Duration::from_secs(60))
            .expect("replay finished");
        if text == "replay complete" {
            break;
        }
        if quality_line(&text) {
            lines.push(text);
        }
    }

    if let Some(path) = std::env::var_os("QTCALIBRATE_LOG") {
        // "DEBUG  1152:magquality: ..." -> "magquality: ..."
        let expected: Vec<String> = std::fs::read_to_string(path)
            .expect("qtcalibrate log")
            .lines()
            .filter_map(|l| l.split_once(':').map(|(_, rest)| rest.to_owned()))
            .filter(|l| quality_line(l))
            .collect();
        assert_eq!(lines.len(), expected.len(), "quality updates");
        for (i, (ours, theirs)) in lines.iter().zip(&expected).enumerate() {
            assert_eq!(ours, theirs, "line {i}");
        }
    }
    assert_eq!(&lines[lines.len() - 4..], LAST);
}
