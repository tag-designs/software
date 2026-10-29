//! The engine's commands, queries and view state, on the committed fixtures.
//!
//! What the plot shows is the C++ core's, checked against the Qt app by
//! `session_cli`; these tests check the engine's own part: that commands
//! reach the session, the view state carries what the page draws, File Info
//! and the status line read as the Qt app writes them, files are read and
//! written, and failures become messages.

use std::path::PathBuf;

use sensorviz_core::engine::{EngineConfig, Viewer};
use serde_json::{Value, json};

/// The committed fixture log `name`.
fn fixture(name: &str) -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../../docs/fixtures/sensorviz")
        .join(name)
}

/// A viewer with fixture `name` opened through `ready`.
fn viewer_with(name: &str) -> Viewer {
    let mut viewer = Viewer::new(EngineConfig {
        home_dir: std::env::temp_dir(),
        initial_log: Some(fixture(name)),
        ..EngineConfig::default()
    })
    .expect("viewer");
    viewer.command("ready", &json!({}));
    viewer
}

/// A scratch file path for this test run.
fn scratch(name: &str) -> PathBuf {
    std::env::temp_dir().join(format!("sensorviz-core-test-{}-{name}", std::process::id()))
}

#[test]
fn opens_and_describes_imutag() {
    let mut viewer = viewer_with("imutag.db3");
    let state = viewer.view_state();
    assert_eq!(state["hasLog"], true);
    assert_eq!(state["fileName"], "imutag.db3");
    assert_eq!(state["timeDomain"], "elapsed");
    assert_eq!(state["hasCompass"], false);
    assert!(state["message"].is_null());
    let info = state["fileInfo"].as_str().unwrap();
    let lines: Vec<&str> = info.lines().collect();
    assert_eq!(
        lines[..3],
        ["imutag.db3", "Tag type: IMUTAG", "Profile: IMUTAG"]
    );
    assert!(lines.contains(&"Compass calibration: loaded"));
    assert!(!info.contains("config:") && !info.contains("\ninfo:"));
    assert!(
        state["status"]
            .as_str()
            .unwrap()
            .ends_with(" streams loaded")
    );

    let spec = &state["spec"];
    assert_eq!(spec["title"], "imutag.db3");
    let traces = spec["traces"].as_array().unwrap();
    assert!(!traces.is_empty());

    let [lower, upper] = [
        spec["xDataRange"][0].as_f64().unwrap(),
        spec["xDataRange"][1].as_f64().unwrap(),
    ];
    let envelope = viewer.query(
        "envelope",
        &json!({ "t0": lower, "t1": upper, "width": 600 }),
    );
    for trace in traces {
        let id = trace["id"].as_str().unwrap();
        let t = envelope["traces"][id]["t"].as_array().unwrap();
        let v = envelope["traces"][id]["v"].as_array().unwrap();
        assert!(!t.is_empty(), "{id}");
        assert_eq!(t.len(), v.len());
    }
    let hover = viewer.query("hover", &json!({ "t": (lower + upper) / 2.0 }));
    assert!(hover["time"].as_str().unwrap().starts_with("Elapsed "));
    assert!(!hover["rows"].as_array().unwrap().is_empty());

    let params: Vec<&str> = state["parameters"]
        .as_array()
        .unwrap()
        .iter()
        .map(|p| p["id"].as_str().unwrap())
        .collect();
    assert_eq!(params, ["altitude.sea_level_mbar"]);
}

#[test]
fn compasstag_has_the_compass() {
    let mut viewer = viewer_with("compasstag.db3");
    let state = viewer.view_state();
    assert_eq!(state["hasCompass"], true);
    assert_eq!(state["timeDomain"], "epoch");
    assert!(
        state["fileInfo"]
            .as_str()
            .unwrap()
            .contains("Compass orientation: derived from raw compass samples")
    );
    assert!(
        state["status"]
            .as_str()
            .unwrap()
            .contains("record sets loaded")
    );

    viewer.command(
        "set_parameter",
        &json!({ "id": "compass.declination_deg", "value": 10.0 }),
    );
    let state = viewer.view_state();
    let declination = state["parameters"]
        .as_array()
        .unwrap()
        .iter()
        .find(|p| p["id"] == "compass.declination_deg")
        .unwrap();
    assert_eq!(declination["value"], 10.0);
    assert!(
        state["spec"]["metadataRows"]
            .as_array()
            .unwrap()
            .iter()
            .any(|r| r.as_str().unwrap().contains("10.00"))
    );

    viewer.command("set_utc_offset", &json!({ "hours": -5 }));
    assert_eq!(viewer.view_state()["spec"]["utcOffsetHours"], -5);
}

#[test]
fn view_commands_reach_the_session() {
    let mut viewer = viewer_with("imutag.db3");
    viewer.command(
        "set_stream_range",
        &json!({ "id": "imu_pressure", "lower": 950.0, "upper": 1010.0 }),
    );
    let state = viewer.view_state();
    assert_eq!(
        state["view"]["customRanges"]["imu_pressure"]["explicit"],
        true
    );
    assert_eq!(state["view"]["customRanges"]["altitude"]["explicit"], false);
    assert_eq!(
        viewer.query("range", &json!({ "id": "imu_pressure" })),
        json!({ "lower": 950.0, "upper": 1010.0 })
    );

    viewer.command(
        "set_stream_range",
        &json!({ "id": "imu_pressure", "lower": 5.0, "upper": 1.0 }),
    );
    let state = viewer.view_state();
    assert_eq!(
        state["message"]["text"],
        "Minimum must be less than maximum."
    );

    viewer.command("reset_zoom", &json!({}));
    assert_eq!(viewer.view_state()["view"]["customRanges"], json!({}));

    viewer.command(
        "set_visible_streams",
        &json!({ "ids": ["imu_ax", "imu_ay"] }),
    );
    let state = viewer.view_state();
    assert_eq!(
        state["view"]["visibleStreamIds"],
        json!(["imu_ax", "imu_ay"])
    );
    assert_eq!(state["spec"]["traces"].as_array().unwrap().len(), 2);

    viewer.command("set_break_at_gaps", &json!({ "on": false }));
    assert_eq!(viewer.view_state()["spec"]["breakAtGaps"], false);
    viewer.command("set_title", &json!({ "text": "Flight 3" }));
    assert_eq!(viewer.view_state()["spec"]["title"], "Flight 3");
    viewer.command("set_title_visible", &json!({ "on": false }));
    assert_eq!(viewer.view_state()["spec"]["title"], "");
    viewer.command("set_colors", &json!({ "colors": { "imu_ax": "#102030" } }));
    viewer.command("set_axis_sides", &json!({ "sides": { "imu_ax": "right" } }));
    let state = viewer.view_state();
    let ax = state["view"]["streams"]
        .as_array()
        .unwrap()
        .iter()
        .find(|s| s["id"] == "imu_ax")
        .unwrap()
        .clone();
    assert_eq!(ax["effectiveColor"], "#102030");
    assert_eq!(ax["effectiveAxisSide"], "right");
}

#[test]
fn preferences_follow_the_tag_through_files() {
    let file = scratch("prefs.json");
    let mut viewer = viewer_with("imutag.db3");
    viewer.command("set_colors", &json!({ "colors": { "imu_ax": "#ff0000" } }));
    viewer.command("store_preferences", &json!({ "path": file }));
    let text = std::fs::read_to_string(&file).unwrap();
    assert!(text.contains("\"IMUTAG\": {"), "{text}");
    assert!(text.contains("\"imu_ax\": \"#ff0000\""), "{text}");

    // A new engine: defaults until the file is loaded.
    let mut fresh = viewer_with("imutag.db3");
    let color = |v: &mut Viewer| -> Value {
        let state = v.view_state();
        state["view"]["streams"]
            .as_array()
            .unwrap()
            .iter()
            .find(|s| s["id"] == "imu_ax")
            .unwrap()["effectiveColor"]
            .clone()
    };
    assert_ne!(color(&mut fresh), "#ff0000");
    fresh.command("load_preferences", &json!({ "path": file }));
    assert_eq!(color(&mut fresh), "#ff0000");
    fresh.command("load_default_preferences", &json!({}));
    assert_ne!(color(&mut fresh), "#ff0000");

    // Remembered across a load of another log and back.
    fresh.command("set_colors", &json!({ "colors": { "imu_ax": "#00ff00" } }));
    fresh.command("open", &json!({ "path": fixture("compasstag.db3") }));
    fresh.command("open", &json!({ "path": fixture("imutag.db3") }));
    assert_eq!(color(&mut fresh), "#00ff00");

    fresh.command(
        "load_preferences",
        &json!({ "path": scratch("missing.json") }),
    );
    assert_eq!(
        fresh.view_state()["message"]["title"],
        "Load Preferences Failed"
    );
    let _ = std::fs::remove_file(file);
}

#[test]
fn failures_become_messages() {
    let mut viewer = viewer_with("compasstag.db3");
    viewer.command("open", &json!({ "path": fixture("missing.db3") }));
    let state = viewer.view_state();
    assert_eq!(state["message"]["title"], "Load Failed");
    assert!(
        state["message"]["text"]
            .as_str()
            .unwrap()
            .starts_with("Could not open database")
    );
    assert_eq!(state["fileName"], "compasstag.db3", "the open log stays");
    let seq = state["message"]["seq"].as_u64().unwrap();
    viewer.command("open", &json!({ "path": fixture("missing.db3") }));
    assert_eq!(
        viewer.view_state()["message"]["seq"].as_u64().unwrap(),
        seq + 1
    );

    let empty = Viewer::new(EngineConfig::default()).unwrap().view_state();
    assert_eq!(empty["hasLog"], false);
    assert_eq!(empty["status"], "No log loaded");
    assert!(empty["spec"].is_null());
}

#[test]
fn saves_files_the_page_makes() {
    let file = scratch("out.pdf");
    let mut viewer = viewer_with("imutag.db3");
    viewer.command(
        "save_file",
        &json!({ "path": file, "base64": "JVBERi0xLjQK" }),
    );
    assert_eq!(std::fs::read(&file).unwrap(), b"%PDF-1.4\n");
    let _ = std::fs::remove_file(file);
}
