//! Replays qtcalibrate's reference capture through [`Session`] as qtcalibrate's
//! own replay feeds it, and requires the figures qtcalibrate logged for the
//! same replay (`qtcalibrate --replay-capture ... --replay-exit --log-file`),
//! printed to the precision its log prints them.

use calibsession::{Session, display_angles, model_rotation};
use serde_json::Value;

/// The capture and qtcalibrate's final log lines for it.
const FIXTURE: &str = "../../docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-145438.json";

/// One sample of the fixture: magnetometer, and accelerometer when present.
type Sample = ([f32; 3], Option<[f32; 3]>);

/// Reads the fixture's samples, in capture order.
fn samples() -> Vec<Sample> {
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join(FIXTURE);
    let doc: Value =
        serde_json::from_str(&std::fs::read_to_string(path).expect("fixture")).expect("json");
    let num = |v: &Value, k: &str| v[k].as_f64().expect("number") as f32;
    doc["samples"]
        .as_array()
        .expect("samples")
        .iter()
        .map(|s| {
            let m = &s["mag"];
            let mag = [num(m, "mx"), num(m, "my"), num(m, "mz")];
            let accel = s["has_accel"].as_bool().unwrap_or(false).then(|| {
                let a = &s["accel"];
                [num(a, "ax"), num(a, "ay"), num(a, "az")]
            });
            (mag, accel)
        })
        .collect()
}

#[test]
fn replay_matches_qtcalibrate() {
    let mut session = Session::new().expect("session");
    let all = samples();
    assert_eq!(all.len(), 775);
    for (i, (mag, accel)) in all.iter().enumerate() {
        // qtcalibrate solves the live compass before feeding the sample;
        // it uses the filter only, so it cannot change the calibration.
        if let Some(a) = accel {
            let _ = session.orientation(*mag, *a);
        }
        session.add_sample(*mag, *accel);
        // Its replay ticks the quality update every second sample.
        if (i + 1) % 2 == 0 {
            session.update_quality();
        }
    }
    // And once more at the end.
    session.update_quality();

    let q = session.quality();
    // magquality: coverage 90/100 isotropy 0.722 attitude 2.06 (185 cells
    // over 90 patches) dip 63.54 +/- 2.70 deg (p95 5.99, 559 of 650 in gate)
    assert_eq!((q.patches_seen, q.mag_patches), (90, 100));
    assert_eq!(format!("{:.3}", q.isotropy), "0.722");
    assert_eq!(format!("{:.2}", q.attitude_diversity), "2.06");
    assert_eq!((q.attitude_cells, q.attitude_patches), (185, 90));
    assert_eq!(
        format!(
            "{:.2} {:.2} {:.2}",
            q.dip_mean_deg, q.dip_spread_deg, q.dip_p95_deg
        ),
        "63.54 2.70 5.99"
    );
    assert_eq!((q.dip_samples, q.samples), (559, 650));

    let g = session.accel_fit();
    // gravity: offset -31.84 -0.80 -74.50 = 81.02 mg radius 994.1 residual
    // 34.96 (691 held over 32/32 patches, isotropy 0.910, 775 offered 84 gated)
    assert!(g.valid);
    assert_eq!(
        format!("{:.2} {:.2} {:.2}", g.offset[0], g.offset[1], g.offset[2]),
        "-31.84 -0.80 -74.50"
    );
    assert_eq!(format!("{:.1} {:.2}", g.radius, g.residual), "994.1 34.96");
    assert_eq!((g.samples, g.patches_seen, g.patches), (691, 32, 32));
    assert_eq!(format!("{:.3}", g.isotropy), "0.910");
    assert_eq!((g.offered, g.gated), (775, 84));

    // retention: 125 evictions (105 by leverage, 20 as outliers)
    let e = session.evictions();
    assert_eq!((e.total, e.leverage, e.outlier), (125, 105, 20));

    assert!(session.has_calibration());
    assert_eq!(session.points().len(), 650);
}

#[test]
fn constants_round_trip_and_clear() {
    let mut session = Session::new().expect("session");
    let c = session.constants();
    assert!(!c.valid);
    assert_eq!(c.field, 50.0);
    assert_eq!(
        c.mapping,
        [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]
    );

    let offset = [1.0, 2.0, 3.0];
    let mapping = [[1.1, 0.0, 0.0], [0.0, 0.9, 0.0], [0.0, 0.0, 1.0]];
    session.set_constants(offset, mapping);
    let c = session.constants();
    assert!(c.valid && c.offset == offset && c.mapping == mapping);
    // No buffered readings: the field is kept.
    assert_eq!(c.field, 50.0);
    assert_eq!(session.calibrate([11.0, 12.0, 13.0]), [11.0, 9.0, 10.0]);

    session.set_accel_offset([10.0, 0.0, -5.0]);
    assert!(session.accel_fit().valid);
    session.clear();
    assert!(!session.has_calibration());
    assert!(!session.accel_fit().valid);
}

#[test]
fn angles_of_simple_rotations() {
    let identity = display_angles([1.0, 0.0, 0.0, 0.0]);
    assert_eq!(
        (identity.yaw, identity.pitch, identity.roll),
        (0.0, 0.0, 0.0)
    );

    // A model rotation of 90 degrees about z is a heading of 270 (the
    // display's yaw is clockwise).
    let q = model_rotation(0.0, 0.0, 90.0);
    let a = display_angles(q);
    assert!((a.yaw - 270.0).abs() < 1e-4, "{a:?}");
    assert!(a.pitch.abs() < 1e-4 && a.roll.abs() < 1e-4, "{a:?}");
}
