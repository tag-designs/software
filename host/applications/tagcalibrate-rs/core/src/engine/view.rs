//! The view state: everything the page draws, built from the [`Calibrator`]
//! after every change, and the menus.
//!
//! The page holds no rules, so every text is formatted here, as qtcalibrate's
//! labels format it (`MainWindow::calibration_update`,
//! `updateMagQualityDisplay`) and as `MyCompass.qml` formats the readouts.
//! The shape is described in `ui/mock-engine.js`, which sends the same.

use calibsession::{display_angles, model_rotation};
use serde_json::{Value, json};

use super::calibrator::{Calibrator, PromptKind};

/// An accelerometer offset this large, in mg, is shown as suspect: the part
/// is specified at +/-11 mg after trim and board stress takes a real tag to
/// about 77 mg (qtcalibrate's `kImplausibleAccelOffsetMg`).
const IMPLAUSIBLE_ACCEL_OFFSET_MG: f32 = 250.0;

/// The window title.
pub(crate) fn title() -> String {
    let mut v = env!("CARGO_PKG_VERSION").split('.');
    format!(
        "Tag Calibrator v{}.{}",
        v.next().unwrap_or("0"),
        v.next().unwrap_or("0")
    )
}

/// A number rounded to a whole one, with thousands separators, as the
/// compass display's `toLocaleString(locale, 'f', 0)` shows it.
fn whole(v: f64) -> String {
    let s = format!("{v:.0}");
    let (sign, digits) = s.strip_prefix('-').map_or(("", s.as_str()), |d| ("-", d));
    let mut out = String::new();
    for (i, c) in digits.chars().enumerate() {
        if i > 0 && (digits.len() - i) % 3 == 0 {
            out.push(',');
        }
        out.push(c);
    }
    format!("{sign}{out}")
}

/// A float with a sign and three decimals, as `%+.3f`.
fn signed(v: f32) -> String {
    format!("{v:+.3}")
}

/// A base in the list to choose from: bus and address, the USB IDs, what the
/// base says it is, and its ST-LINK serial (Tag Monitor's
/// `describe_base_with_identity`).
fn describe_base(base: &tagcore::UsbDev, identity: Option<&str>) -> String {
    let field = |id: &str, key: &str| {
        id.split(';')
            .find_map(|f| f.strip_prefix(key)?.strip_prefix('='))
            .filter(|v| !v.is_empty())
            .map(str::to_owned)
    };
    let mut text = format!(
        "{:03}:{:03}    0x{:x}:0x{:x}",
        base.bus, base.address, base.vid, base.pid
    );
    if let Some(id) = identity {
        text += &format!(
            "    {}",
            field(id, "base").unwrap_or_else(|| "tag base".into())
        );
        if let Some(fw) = field(id, "fw") {
            let dirty = if field(id, "dirty").as_deref() == Some("1") {
                "+"
            } else {
                ""
            };
            text += &format!(" fw {fw}{dirty}");
        }
    }
    if !base.serial.is_empty() {
        text += &format!("  (ST-LINK {})", base.serial);
    }
    text
}

impl Calibrator {
    /// The whole view state.
    pub fn view_state(&self) -> Value {
        let tag_text = match (&self.replay, &self.info) {
            (Some(r), _) => format!("Replay: {}", r.name),
            (None, Some(info)) => {
                let t = tagcore::proto::TagType::try_from(info.tag_type)
                    .map(|t| t.as_str_name())
                    .unwrap_or("");
                format!("{t}  ·  UUID {}", info.uuid)
            }
            _ => String::new(),
        };
        let replaying = self.replaying();
        let has_cal = self.session.has_calibration();
        let action = |enabled: bool| json!({ "visible": true, "enabled": enabled });
        json!({
            "title": title(),
            "link": { "attached": self.attached, "text": tag_text },
            "stream": { "on": self.streaming, "enabled": self.attached },
            "actions": {
                "attach": action(!self.attached && !self.attaching && !replaying),
                "detach": action(self.attached),
                "start": action(self.streaming && !self.collecting),
                "stop": action(self.collecting),
                "clear": action(self.streaming && !self.collecting),
                "tag_write": action(self.attached && !self.streaming && !replaying && has_cal),
                "tag_read": action(self.attached && !self.streaming && !replaying),
                "save_capture": action(self.capture.ready()),
            },
            "calibration": self.calibration_view(),
            "quality": self.quality_view(),
            "sphere": self.sphere_view(),
            "orientation": self.orientation_view(),
            "settings": {
                "declination": self.declination,
                "declinationText": format!("{:.2}", self.declination),
                "batteryForward": self.battery_forward,
            },
            "prompt": self.prompt_view(),
        })
    }

    /// The fitted constants, "--" until a calibration exists.
    fn calibration_view(&self) -> Value {
        let none = "--";
        let accel = {
            let g = self.session.accel_fit();
            if g.valid {
                let [x, y, z] = g.offset;
                let magnitude = x.hypot(y).hypot(z);
                let warn = magnitude > IMPLAUSIBLE_ACCEL_OFFSET_MG;
                json!({
                    "text": format!("{x:.1} {y:.1} {z:.1} = {magnitude:.1} mg{}", if warn { "  CHECK" } else { "" }),
                    "warn": warn,
                })
            } else {
                json!({ "text": none, "warn": false })
            }
        };
        let c = self.session.constants();
        if !c.valid {
            return json!({
                "offset": [none, none, none],
                "mapping": ["-- -- --", "-- -- --", "-- -- --"],
                "field": none,
                "accelOffset": accel,
            });
        }
        json!({
            "offset": c.offset.map(signed),
            "mapping": c.mapping.map(|r| r.map(signed).join(" ")),
            "field": format!("{:.2}", c.field),
            "accelOffset": accel,
        })
    }

    /// The quality figures, "--" before the first update.
    fn quality_view(&self) -> Value {
        let q = self.session.quality();
        let none = json!({
            "spread": "--", "worst5": "--", "hardIron": "--", "fitError": "--",
            "coverage": "--", "evenness": "--", "attitude": "--", "dip": "--",
        });
        if !self.quality_shown {
            return none;
        }
        let dip = if q.have_accel_metrics {
            format!("{:.1} ± {:.1}", q.dip_mean_deg, q.dip_spread_deg)
        } else {
            "no accel".to_owned()
        };
        let residual = |v: f32| {
            if q.samples > 0 {
                format!("{v:.2}")
            } else {
                "--".to_owned()
            }
        };
        json!({
            "spread": residual(q.residual_spread),
            "worst5": residual(q.residual_p95),
            "hardIron": residual(q.residual_hard_iron),
            "fitError": residual(q.fit_error),
            "coverage": format!("{}/{}", q.patches_seen, q.mag_patches),
            "evenness": format!("{:.2}", q.isotropy),
            "attitude": format!("{:.2}", q.attitude_diversity),
            "dip": dip,
        })
    }

    /// The sphere: the buffered readings, calibrated, on a sphere of the field.
    fn sphere_view(&self) -> Value {
        let points: Vec<f32> = self.session.points().into_iter().flatten().collect();
        json!({
            "radius": self.session.field(),
            "points": points,
            "newest": self.newest,
        })
    }

    /// The compass: the dial's heading, the tag model's rotation and the
    /// readouts, with `MyCompass.qml`'s declination and battery rules.
    fn orientation_view(&self) -> Value {
        let Some(live) = self.live.filter(|_| self.streaming) else {
            return json!({
                "valid": false, "heading": null, "quaternion": [1.0, 0.0, 0.0, 0.0],
                "readouts": { "heading": "", "roll": "", "pitch": "", "dip": "", "field": "", "gravity": "" },
            });
        };
        let o = live.orientation;
        let a = display_angles(o.q);
        // MyCompass.qml: heading plus declination; battery backward turns the
        // heading round and flips pitch and roll.
        let (mut h, mut p, mut r) = (a.yaw + self.declination, a.pitch, a.roll);
        if !self.battery_forward {
            h += 180.0;
            p = -p;
            r = -r;
        }
        h = (h + 360.0) % 360.0;
        // qtcalibrate turns its tag model by the raw angles, yaw
        // counter-clockwise.
        let model = model_rotation(a.pitch as f32, a.roll as f32, (360.0 - a.yaw) as f32);
        json!({
            "valid": true,
            "heading": h,
            "quaternion": model,
            "readouts": {
                "heading": whole(h),
                "roll": whole(r),
                "pitch": whole(p),
                "dip": whole(f64::from(o.dip)),
                "field": whole(f64::from(o.field)),
                "gravity": whole(f64::from(live.gravity)),
            },
        })
    }

    /// The prompt on screen, or null.
    fn prompt_view(&self) -> Value {
        let Some(p) = &self.prompt else {
            return Value::Null;
        };
        let mut v = match &p.kind {
            PromptKind::Alert { title, text, style } => json!({
                "kind": "alert", "title": title, "text": text, "informative": "", "style": style,
            }),
            PromptKind::ChooseBase(bases) => json!({
                "kind": "chooseBase",
                "title": "Available Bases",
                "label": "Please select a base",
                "items": bases.iter().map(|(b, id)| describe_base(b, id.as_deref())).collect::<Vec<_>>(),
            }),
            PromptKind::SaveCapture => {
                let when = self
                    .capture
                    .stopped_ms
                    .unwrap_or_else(super::capture::now_ms);
                let name = format!("qtcalibrate-samples-{}.json", super::capture::stamp(when));
                json!({
                    "kind": "saveFile",
                    "title": "Save Sample Capture",
                    "suggested": self.config.home_dir.join(name),
                    "filters": [{ "name": "JSON", "extensions": ["json"] }],
                })
            }
        };
        v["id"] = json!(p.id);
        v
    }
}

/// The `menus` query's answer: the menu bar as data.
pub(crate) fn menus() -> Value {
    json!({ "menus": [
        { "title": "File", "entries": [
            { "kind": "command", "id": "save_capture", "label": "Save Sample Capture...", "tooltip": "Save the stopped capture's samples and fit to a JSON file" },
            { "kind": "item", "id": "save_error_log", "label": "Save Log...", "tooltip": "Save the log to a text file" },
            { "kind": "separator" },
            { "kind": "item", "id": "quit", "label": "Quit", "tooltip": "", "shortcut": "quit" },
        ] },
        { "title": "Tag", "entries": [
            { "kind": "command", "id": "attach", "label": "Attach", "tooltip": "Find a tag base and attach to its tag" },
            { "kind": "command", "id": "detach", "label": "Detach", "tooltip": "Detach from the tag" },
            { "kind": "item", "id": "stream", "label": "Stream", "tooltip": "Read the tag's sensors continuously" },
            { "kind": "separator" },
            { "kind": "command", "id": "start", "label": "Start", "tooltip": "Start collecting calibration samples" },
            { "kind": "command", "id": "stop", "label": "Stop", "tooltip": "Stop collecting" },
            { "kind": "command", "id": "clear", "label": "Clear", "tooltip": "Discard the samples and the fit" },
            { "kind": "separator" },
            { "kind": "command", "id": "tag_write", "label": "Tag Write", "tooltip": "Write the fitted constants to the tag" },
            { "kind": "command", "id": "tag_read", "label": "Tag Read", "tooltip": "Read the tag's stored constants" },
        ] },
        { "title": "Configuration", "entries": [
            { "kind": "item", "id": "declination", "label": "Declination...", "tooltip": "The local magnetic declination, added to the heading" },
            { "kind": "item", "id": "battery_forward", "label": "Battery Forward", "tooltip": "Show the tag with its battery end forward" },
        ] },
        { "title": "Log", "entries": [
            { "kind": "item", "id": "log_level", "label": "Log Level", "tooltip": "" },
            { "kind": "item", "id": "clear_error_log", "label": "Clear Log", "tooltip": "Clear the log" },
        ] },
        { "title": "Help", "entries": [
            { "kind": "item", "id": "user_guide", "label": "Tag Calibrator User Guide", "tooltip": "Open the Tag Calibrator page of the user manual" },
            { "kind": "item", "id": "about", "label": "About Tag Calibrator", "tooltip": "Version and purpose" },
        ] },
    ] })
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Whole numbers with separators, as the compass shows them.
    #[test]
    fn whole_numbers() {
        assert_eq!(whole(1038.4), "1,038");
        assert_eq!(whole(315.0), "315");
        assert_eq!(whole(-18.2), "-18");
        assert_eq!(whole(1_234_567.0), "1,234,567");
    }
}
