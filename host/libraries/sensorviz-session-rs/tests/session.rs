//! The Rust interface against sensorViz's core, on the committed fixtures.
//!
//! The core itself is checked by sensorViz's own checks and by comparing
//! `session_cli` with the Qt app; these tests check that the interface
//! carries it across intact: the descriptions parse, envelopes and samples
//! arrive whole, setters report their changes, errors come back, and a
//! session driven through the Rust setters reaches exactly the state the
//! core's own script reaches.

use sensorviz_session::{AxisMode, Changes, Preferences, Session, Side, TimeDomain};

/// The committed fixture log `name`.
fn fixture(name: &str) -> String {
    format!(
        "{}/../../docs/fixtures/sensorviz/{name}",
        env!("CARGO_MANIFEST_DIR")
    )
}

/// A session with fixture `name` open, without preferences.
fn opened(name: &str) -> Session {
    let mut session = Session::new().expect("session");
    session.open(fixture(name), None).expect("open");
    session
}

/// IMUTag: the catalog, view and spec, and envelopes for every trace.
#[test]
fn imutag_describes_and_draws() {
    let mut session = opened("imutag.db3");
    let catalog = session.catalog().expect("catalog");
    assert_eq!(catalog.tag_type, "IMUTAG");
    assert_eq!(catalog.time_domain, TimeDomain::Elapsed);
    assert!(catalog.collection_start_epoch_ms.is_some());
    assert!(catalog.compass_calibration.present);
    assert!(catalog.transform_failures.is_empty());
    assert!(catalog.compass_source.is_empty());

    let view = session.view().expect("view");
    assert!(view.streams.iter().any(|s| s.id == "altitude"));
    assert!(view.streams.iter().any(|s| s.id == "imu_accel_magnitude"));
    assert_eq!(view.title, "imutag.db3");
    assert!(view.break_at_gaps);

    let spec = session.spec().expect("spec");
    assert!(!spec.traces.is_empty());
    assert_eq!(spec.title, "imutag.db3");
    let [lower, upper] = spec.x_data_range.expect("x range");
    for trace in &spec.traces {
        let envelope = session.envelope(
            trace.stream,
            f64::NEG_INFINITY,
            f64::INFINITY,
            800,
            spec.break_at_gaps,
        );
        assert!(!envelope.time.is_empty(), "{} has points", trace.id);
        assert_eq!(envelope.time.len(), envelope.value.len());
        assert_eq!(envelope.time.len(), envelope.index.len());
        assert!(envelope.time.iter().all(|t| *t >= lower && *t <= upper));
        assert!(trace.axis < spec.axes.len());
    }
    assert!(spec.axes.iter().all(|axis| !axis.traces.is_empty()));

    // An unknown handle has no points.
    assert!(
        session
            .envelope(u64::MAX - 1, 0.0, 1.0, 10, true)
            .time
            .is_empty()
    );

    let hover = session.hover((lower + upper) / 2.0).expect("hover");
    assert!(hover[0].starts_with("Elapsed "));
    assert_eq!(hover[0], session.format_time((lower + upper) / 2.0));
    assert!(hover.len() > 1);
}

/// CompassTag: the compass samples and the parameters.
#[test]
fn compasstag_compass_and_parameters() {
    let mut session = opened("compasstag.db3");
    let catalog = session.catalog().expect("catalog");
    assert_eq!(catalog.profile_name, "CompassTag");
    assert_eq!(catalog.compass_source, "derived from raw compass samples");
    assert!(catalog.record_sets.iter().any(|r| r.id == "compass_raw"));

    let samples = session.compass_samples();
    assert_eq!(samples.len(), 44);
    assert!(samples.windows(2).all(|w| w[0].time <= w[1].time));

    let parameters = session.parameters().expect("parameters");
    let ids: Vec<&str> = parameters.iter().map(|p| p.id.as_str()).collect();
    assert_eq!(ids, ["compass.declination_deg", "compass.battery_forward"]);
    assert_eq!(parameters[1].kind, "toggle");
    assert_eq!(parameters[1].value, 1.0);

    // Declination remakes the heading only: its stream and the parameter change.
    let heading_before = session
        .view()
        .unwrap()
        .streams
        .into_iter()
        .find(|s| s.id == "compass_heading")
        .unwrap();
    let changes = session
        .set_parameter("compass.declination_deg", 12.5)
        .expect("declination");
    assert!(changes.contains(Changes::PARAMETERS | Changes::STREAMS));
    let view = session.view().unwrap();
    let heading_after = view
        .streams
        .iter()
        .find(|s| s.id == "compass_heading")
        .unwrap();
    assert_ne!(heading_before.stream, heading_after.stream);
    assert_eq!(view.parameters["compass.declination_deg"], 12.5);
    assert!(
        session
            .spec()
            .unwrap()
            .metadata_rows
            .iter()
            .any(|row| row.contains("12.50"))
    );
}

/// Setters report what they changed, and nothing when nothing did.
#[test]
fn setters_report_changes() {
    let mut session = opened("imutag.db3");
    let changes = session.set_stream_range("imu_pressure", 950.0, 1010.0);
    assert!(changes.contains(Changes::RANGES));
    let view = session.view().unwrap();
    assert!(view.custom_ranges["imu_pressure"].explicit);
    assert!(
        !view.custom_ranges["altitude"].explicit,
        "altitude is linked to pressure"
    );
    assert_eq!(
        session.range_for_dialog("imu_pressure"),
        Some((950.0, 1010.0))
    );
    assert_eq!(session.range_for_dialog("nonsense"), None);
    let fixed = session.spec().unwrap();
    assert!(fixed.axes.iter().any(|axis| axis.mode == AxisMode::Fixed));

    assert_eq!(session.reset_zoom(), Changes::RANGES);
    assert!(session.reset_zoom().is_empty());

    assert_eq!(session.set_utc_offset(3), Changes::UTC_OFFSET);
    assert!(session.set_utc_offset(3).is_empty());
    assert_eq!(session.set_break_at_gaps(false), Changes::GAPS);
    assert!(session.set_title("T").unwrap().contains(Changes::TITLE));
    assert!(
        session
            .set_colors(&[("imu_pressure", 0x112233)])
            .unwrap()
            .contains(Changes::APPEARANCE)
    );
    assert_eq!(
        session
            .view()
            .unwrap()
            .streams
            .iter()
            .find(|s| s.id == "imu_pressure")
            .unwrap()
            .effective_color,
        "#112233"
    );
    assert!(
        session
            .set_axis_sides(&[("imu_pressure", Side::Right)])
            .unwrap()
            .contains(Changes::APPEARANCE)
    );
    assert!(
        session
            .set_visible_streams(&["imu_ax"])
            .unwrap()
            .contains(Changes::VISIBILITY)
    );
    assert_eq!(session.view().unwrap().visible_stream_ids, ["imu_ax"]);
    assert!(
        session
            .set_stream_visible("imu_pressure", true)
            .contains(Changes::VISIBILITY)
    );
}

/// Driven through the Rust setters, a session reaches exactly the state the
/// core's own state-dump script reaches.
#[test]
fn rust_setters_match_the_core_script() {
    for name in ["imutag.db3", "compasstag.db3"] {
        let mut scripted = opened(name);
        scripted.state_dump();

        let mut driven = opened(name);
        let elapsed = driven.catalog().unwrap().time_domain == TimeDomain::Elapsed;
        driven
            .set_parameter("altitude.sea_level_mbar", 1000.0)
            .unwrap();
        let pressure = if driven
            .view()
            .unwrap()
            .streams
            .iter()
            .any(|s| s.id == "pressure")
        {
            "pressure"
        } else {
            "imu_pressure"
        };
        driven.set_stream_range(pressure, 950.0, 1010.0);
        driven
            .set_parameter("compass.declination_deg", 12.5)
            .unwrap();
        let forward = driven.view().unwrap().parameters["compass.battery_forward"];
        driven
            .set_parameter(
                "compass.battery_forward",
                if forward != 0.0 { 0.0 } else { 1.0 },
            )
            .unwrap();
        driven.set_break_at_gaps(false);
        if !elapsed {
            driven.set_utc_offset(-5);
        }
        driven.reset_zoom();

        assert_eq!(driven.state_json(), scripted.state_json(), "{name}");
    }
}

/// Preferences: loaded, applied on open, remembered, saved as the Qt app writes them.
#[test]
fn preferences_round_trip() {
    let mut preferences = Preferences::new().unwrap();
    let file = r##"{"tags": {"IMUTAG": {"colors": {"imu_ax": "#f00"}, "axis_sides": {"imu_ax": " Right "}}}, "version": 1}"##;
    preferences.load(file, "test.json").expect("load");

    let mut session = Session::new().unwrap();
    session
        .open(fixture("imutag.db3"), Some(&preferences))
        .unwrap();
    let view = session.view().unwrap();
    let ax = view.streams.iter().find(|s| s.id == "imu_ax").unwrap();
    assert_eq!(ax.effective_color, "#ff0000");
    assert_eq!(ax.effective_axis_side, Side::Right);

    session.set_colors(&[("imu_ax", 0x00ff00)]).unwrap();
    session.remember_preferences(&mut preferences);
    let saved = preferences.save();
    assert!(saved.contains("\"imu_ax\": \"#00ff00\""), "{saved}");
    assert!(saved.starts_with("{\n    \"tags\": {\n"));

    session.forget_preferences(&mut preferences);
    assert_eq!(
        preferences.save(),
        "{\n    \"tags\": {\n    },\n    \"version\": 1\n}\n"
    );
    assert!(
        session
            .apply_preferences(&preferences)
            .contains(Changes::APPEARANCE)
    );
}

/// Failures come back as errors, leaving things as they were.
#[test]
fn errors_are_reported() {
    let mut session = opened("compasstag.db3");
    let missing = session.open(fixture("missing.db3"), None).unwrap_err();
    assert!(
        missing.to_string().starts_with("Could not open database"),
        "{missing}"
    );
    assert_eq!(
        session.catalog().unwrap().profile_name,
        "CompassTag",
        "the open log stays"
    );

    let mut preferences = Preferences::new().unwrap();
    let refused = preferences
        .load(r#"{"version": 2, "tags": {}}"#, "f.json")
        .unwrap_err();
    assert_eq!(
        refused.to_string(),
        "Unsupported sensorViz preferences version in f.json"
    );
    assert!(session.set_title("a\0b").is_err());
}
