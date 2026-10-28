//! The calibration sample capture: what Start records, the file Save Sample
//! Capture writes, and reading one back for a replay.
//!
//! The file is qtcalibrate's (`MainWindow::saveSampleCapture`), schema
//! `tag-designs.qtcalibrate.calibration-capture.v1`, written the way Qt's
//! `QJsonDocument::Indented` writes it -- keys sorted, four-space indent,
//! each float written as the double it widens to -- so a capture from either
//! program replays in both and the committed fixtures stay one format.

use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};

use serde_json::{Map, Value, json};
use tagcore::proto::{TagInfo, TagType};

use super::tagthread::Sample;

/// The capture schema qtcalibrate writes and reads.
pub(crate) const SCHEMA: &str = "tag-designs.qtcalibrate.calibration-capture.v1";

/// One recorded sample, with where it came from.
#[derive(Debug, Clone)]
pub(crate) struct CaptureSample {
    /// Which calibration-log read it came in (or its index, in a replay).
    pub batch_index: i32,
    /// Its position in that read.
    pub sample_index: i32,
    /// The raw sample.
    pub sample: Sample,
}

/// The capture between Start and Stop, and whether it can be saved.
#[derive(Debug, Default)]
pub(crate) struct Capture {
    /// The recorded samples.
    pub samples: Vec<CaptureSample>,
    /// When Start was pressed, ms since the epoch.
    pub started_ms: Option<i64>,
    /// When it stopped, ms since the epoch.
    pub stopped_ms: Option<i64>,
    /// Recording.
    pub active: bool,
}

impl Capture {
    /// Starts recording afresh (Start).
    pub fn begin(&mut self) {
        *self = Capture {
            started_ms: Some(now_ms()),
            active: true,
            ..Capture::default()
        };
    }

    /// Stops recording (Stop, Stream off, Detach); nothing if not recording.
    pub fn finish(&mut self) {
        if self.active {
            self.active = false;
            self.stopped_ms = Some(now_ms());
            log::info!(
                "Calibration sample capture stopped with {} samples",
                self.samples.len()
            );
        }
    }

    /// Whether a stopped capture with samples is there to save.
    pub fn ready(&self) -> bool {
        !self.active && !self.samples.is_empty()
    }
}

/// The host clock, ms since the epoch.
pub(crate) fn now_ms() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

/// Days since 1970-01-01 as a civil (year, month, day), proleptic Gregorian
/// (Howard Hinnant's `civil_from_days`).
fn civil(days: i64) -> (i64, u32, u32) {
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = (doy - (153 * mp + 2) / 5 + 1) as u32;
    let m = if mp < 10 { mp + 3 } else { mp - 9 } as u32;
    (yoe + era * 400 + i64::from(m <= 2), m, d)
}

/// ms since the epoch as Qt's `Qt::ISODateWithMs` in UTC:
/// `2026-10-10T14:54:38.123Z`.
pub(crate) fn iso_ms(ms: i64) -> String {
    let (secs, millis) = (ms.div_euclid(1000), ms.rem_euclid(1000));
    let (days, sod) = (secs.div_euclid(86_400), secs.rem_euclid(86_400));
    let (y, m, d) = civil(days);
    format!(
        "{y:04}-{m:02}-{d:02}T{:02}:{:02}:{:02}.{millis:03}Z",
        sod / 3600,
        sod % 3600 / 60,
        sod % 60
    )
}

/// ms since the epoch as `yyyyMMdd-HHmmss` in UTC, for file names.
pub(crate) fn stamp(ms: i64) -> String {
    let iso = iso_ms(ms);
    format!(
        "{}{}{}-{}{}{}",
        &iso[0..4],
        &iso[5..7],
        &iso[8..10],
        &iso[11..13],
        &iso[14..16],
        &iso[17..19]
    )
}

/// A float as the JSON number Qt writes for it: the double it widens to.
pub(crate) fn num(v: f32) -> Value {
    json!(f64::from(v))
}

/// The capture's "tag" block: a subset of TagInfo with neither the uuid nor
/// the source path, so a capture is safe to commit as a fixture
/// (host/docs/fixtures/qtcalibrate/README.md).
pub(crate) fn tag_json(info: &TagInfo) -> Value {
    let tag_type = TagType::try_from(info.tag_type)
        .map(|t| t.as_str_name())
        .unwrap_or("");
    json!({
        "tag_type": tag_type,
        "board_desc": info.board_desc,
        "firmware": info.firmware,
        "githash": info.githash,
        "build_time": info.build_time,
        "mag_constant": num(info.magconstant),
        "accel_constant": num(info.accelconstant),
    })
}

/// The capture document, given the "calibration" block the caller built.
pub(crate) fn document(capture: &Capture, tag: Option<&Value>, calibration: Value) -> Value {
    let mut root = Map::new();
    root.insert("schema".into(), json!(SCHEMA));
    root.insert("application".into(), json!("qtcalibrate"));
    root.insert("created_at_utc".into(), json!(iso_ms(now_ms())));
    root.insert(
        "capture_started_utc".into(),
        json!(capture.started_ms.map(iso_ms).unwrap_or_default()),
    );
    root.insert(
        "capture_stopped_utc".into(),
        json!(capture.stopped_ms.map(iso_ms).unwrap_or_default()),
    );
    root.insert("sample_count".into(), json!(capture.samples.len()));
    if let Some(tag) = tag {
        root.insert("tag".into(), tag.clone());
    }
    root.insert(
        "units".into(),
        json!({
            "mag": "calibration stream magnetometer units",
            "accel": "calibration stream accelerometer units",
        }),
    );
    let samples: Vec<Value> = capture
        .samples
        .iter()
        .enumerate()
        .map(|(i, s)| {
            let (m, a) = s.sample;
            let mut o = Map::new();
            o.insert("sequence".into(), json!(i));
            o.insert("batch_index".into(), json!(s.batch_index));
            o.insert("sample_index".into(), json!(s.sample_index));
            o.insert(
                "mag".into(),
                json!({ "mx": num(m[0]), "my": num(m[1]), "mz": num(m[2]) }),
            );
            o.insert("has_accel".into(), json!(a.is_some()));
            if let Some(a) = a {
                o.insert(
                    "accel".into(),
                    json!({ "ax": num(a[0]), "ay": num(a[1]), "az": num(a[2]) }),
                );
            }
            Value::Object(o)
        })
        .collect();
    root.insert("samples".into(), Value::Array(samples));
    root.insert("calibration".into(), calibration);
    Value::Object(root)
}

/// Writes a document as Qt's `QJsonDocument::Indented` does: four-space
/// indent, keys in order (serde_json's map keeps them sorted, as
/// `QJsonObject` does), a final newline.
///
/// # Errors
///
/// The write's error, as text.
pub(crate) fn write(path: &Path, doc: &Value) -> Result<(), String> {
    use serde::Serialize;
    let mut out = Vec::new();
    let formatter = serde_json::ser::PrettyFormatter::with_indent(b"    ");
    let mut ser = serde_json::Serializer::with_formatter(&mut out, formatter);
    doc.serialize(&mut ser).map_err(|e| e.to_string())?;
    out.push(b'\n');
    std::fs::write(path, out).map_err(|e| format!("{}: {e}", path.display()))
}

/// A capture read for replay: its samples, and its "tag" block if it has one.
#[derive(Debug)]
pub(crate) struct Replay {
    /// The samples, in order.
    pub samples: Vec<Sample>,
    /// The capture's "tag" block, written back into a capture saved from it.
    pub tag: Option<Value>,
    /// The file's name, for the window.
    pub name: String,
    /// The next sample to feed.
    pub cursor: usize,
}

/// Reads a capture for replay.
///
/// # Errors
///
/// When the file cannot be read or parsed, has another schema, or has no
/// usable sample; the text says which.
pub(crate) fn load(path: &Path) -> Result<Replay, String> {
    let text = std::fs::read_to_string(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let doc: Value = serde_json::from_str(&text).map_err(|e| format!("{}: {e}", path.display()))?;
    if doc["schema"] != SCHEMA {
        return Err(format!(
            "{}: not a calibration capture ({SCHEMA})",
            path.display()
        ));
    }
    let f = |v: &Value, k: &str| v[k].as_f64().map(|x| x as f32);
    let samples: Vec<Sample> = doc["samples"]
        .as_array()
        .map(|a| a.as_slice())
        .unwrap_or_default()
        .iter()
        .filter_map(|s| {
            let m = &s["mag"];
            let mag = [f(m, "mx")?, f(m, "my")?, f(m, "mz")?];
            let accel = if s["has_accel"].as_bool().unwrap_or(false) {
                let a = &s["accel"];
                Some([f(a, "ax")?, f(a, "ay")?, f(a, "az")?])
            } else {
                None
            };
            Some((mag, accel))
        })
        .collect();
    if samples.is_empty() {
        return Err(format!("{}: no usable samples", path.display()));
    }
    Ok(Replay {
        samples,
        tag: doc.get("tag").filter(|t| t.is_object()).cloned(),
        name: path
            .file_name()
            .map(|n| n.to_string_lossy().into_owned())
            .unwrap_or_default(),
        cursor: 0,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Dates as Qt writes them, including a leap day and the epoch.
    #[test]
    fn iso_dates() {
        assert_eq!(iso_ms(0), "1970-01-01T00:00:00.000Z");
        assert_eq!(iso_ms(1_791_464_400_123), "2026-10-08T13:00:00.123Z");
        assert_eq!(iso_ms(951_782_400_000), "2000-02-29T00:00:00.000Z");
        assert_eq!(stamp(1_791_464_400_123), "20261008-130000");
    }
}
