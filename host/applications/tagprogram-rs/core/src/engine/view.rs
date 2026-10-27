//! The view state: everything the window shows, as the contract's `state`
//! event. qtprogram's `MainWindow` display rules (`showCheck`, `showTag`,
//! `showFirmware`, `updateButtons`, `sessionStep`, in `mainwindow.cpp`) are
//! applied here, so the UI only draws.
//!
//! [`Station::view_state`] reads the station and never changes it, and talks
//! to no device. The engine loop (`super::run`) calls it on the engine thread
//! after every message and timer pass, compares the result with the last one
//! sent, and publishes it only when it differs, so it must stay cheap and
//! deterministic: no clock reads, and no I/O beyond the release catalog's
//! checks of its cache directory (`Catalog::is_cached` and `targets` look
//! for files on every call). The JSON's shape is specified in
//! `design/ui-engine-contract.md`.

use std::path::Path;

use serde_json::{Value, json};
use tagcore::proto::TagState;

use super::session::Step;
use super::station::{
    CHECK_NAMES, CLOCK_TOLERANCE_S, Check, Level, Station, state_name, test_name,
};

/// The window title (the `about` query's text repeats its version).
const TITLE: &str = "Tag Programmer v2.0";
/// An em dash: what the tag panel shows for a field it does not know.
const DASH: &str = "\u{2014}";

/// The clock offset as qtprogram's `QString::asprintf` printed it:
/// `%+.2f s` for an offset under 1000 s, else `%+.3g s` (a two-digit
/// exponent, trailing zeros trimmed).
fn clock_text(offset: f64) -> String {
    if offset.abs() < 1000.0 {
        return format!("{offset:+.2} s");
    }
    let exp = offset.abs().log10().floor() as i32;
    let mantissa = offset / 10f64.powi(exp);
    let m = format!("{mantissa:+.2}");
    let m = m.trim_end_matches('0').trim_end_matches('.');
    format!("{m}e+{exp:02} s")
}

/// A firmware combo item for a local file: id `file:<path>`, text
/// `"<file name>  (<directory>)"`, as the firmware combo lists a file.
fn file_item(path: &str) -> Value {
    let p = Path::new(path);
    let name = p
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default();
    let dir = p
        .parent()
        .map(|d| d.to_string_lossy().into_owned())
        .unwrap_or_default();
    json!({ "id": format!("file:{path}"), "text": format!("{name}  ({dir})") })
}

impl Station {
    /// The firmware combo (qtprogram's `showFirmware`): every known release
    /// with its date and flags, the chosen release even if the catalog no
    /// longer lists it, the recent files (the chosen one first), then "Other
    /// ELF file...". Disabled during a session.
    fn firmware_view(&self, running: bool) -> Value {
        let chosen_release = self.chosen_release();
        let chosen_file = self.chosen_file();
        let mut items = Vec::new();
        let releases = self.catalog.releases();
        for r in &releases {
            let mut text = r.tag.clone();
            if let Some(date) = r.published {
                text += &format!("  ({})", date.format("%Y-%m-%d"));
            }
            if r.prerelease {
                text += "  prerelease";
            }
            if !self.catalog.is_cached(&r.tag) {
                text += "  not downloaded";
            }
            items.push(json!({ "id": format!("release:{}", r.tag), "text": text }));
        }
        if let Some(rel) = &chosen_release
            && !releases.iter().any(|r| &r.tag == rel)
        {
            items.push(json!({ "id": format!("release:{rel}"), "text": rel }));
        }
        let mut files = self.settings.list(crate::model::settings::RECENT_FILES);
        if let Some(f) = &chosen_file
            && !files.contains(f)
        {
            files.insert(0, f.clone());
        }
        if !files.is_empty() {
            items.push(json!({ "separator": true }));
            items.extend(files.iter().map(|f| file_item(f)));
        }
        items.push(json!({ "separator": true }));
        items.push(json!({ "id": "other", "text": "Other ELF file..." }));

        let selected = match (&chosen_release, &chosen_file) {
            (Some(r), _) => format!("release:{r}"),
            (None, Some(f)) => format!("file:{f}"),
            _ => String::new(),
        };
        let notes = chosen_release
            .as_ref()
            .and_then(|r| self.catalog.release(r))
            .map(|r| r.page_url)
            .filter(|u| !u.is_empty());
        json!({
            "items": items,
            "selected": selected,
            "enabled": !running,
            "releaseNotesUrl": notes,
        })
    }

    /// The tag type combo: the chosen release's targets, plus the current type
    /// if it is not among them, with a note on where the type came from.
    /// Enabled only for a release (a file's identity record fixes the type)
    /// and not during a session.
    fn tag_type_view(&self, running: bool) -> Value {
        let release = self.chosen_release();
        let mut items = release
            .as_ref()
            .map(|r| self.catalog.targets(r))
            .unwrap_or_default();
        if !self.target.is_empty() && !items.contains(&self.target) {
            items.push(self.target.clone());
        }
        let note = if self.chosen_file().is_some() {
            "from the image"
        } else if self.target_detected {
            "detected from the tag"
        } else {
            ""
        };
        json!({
            "items": items,
            "selected": (!self.target.is_empty()).then(|| self.target.clone()),
            "enabled": !running && release.is_some(),
            "note": note,
        })
    }

    /// The tag panel (qtprogram's `showTag`): UUID, state, firmware, tests and
    /// clock offset while the firmware answers, else dashes; the label recorded
    /// for the tag's UUID, else the last label used; Sync's enablement; and the
    /// last session's result line.
    fn tag_view(&self, running: bool) -> Value {
        let answers = self.attached() && self.info.is_some();
        let mut label = self.last_label.clone();
        let mut out = json!({
            "uuid": DASH, "state": DASH, "firmware": DASH, "tests": DASH,
            "clock": { "text": DASH, "warn": false },
        });
        if let (true, Some(info)) = (answers, &self.info) {
            out["uuid"] = json!(info.uuid);
            out["firmware"] = json!(format!(
                "{}  {}  ({})",
                self.tag_target().unwrap_or_default(),
                info.githash,
                info.board_desc
            ));
            if let Some(status) = &self.status {
                out["state"] = json!(state_name(status.state));
                out["tests"] = json!(test_name(status.test_status));
            }
            if let Some(offset) = self.offset {
                let warn = offset.abs() > CLOCK_TOLERANCE_S;
                let mut text = clock_text(offset);
                if warn {
                    text += "  (press Sync)";
                }
                out["clock"] = json!({ "text": text, "warn": warn });
            }
            if let Some(last) = self
                .record
                .as_ref()
                .and_then(|r| r.labels_for_uuid(&info.uuid).pop())
            {
                label = format!("{last} (recorded)");
            }
        }
        out["label"] = json!(if label.is_empty() {
            DASH.to_owned()
        } else {
            label
        });
        out["syncEnabled"] = json!(!running && answers);
        out["result"] = json!({ "text": self.result.0, "ok": self.result.1 });
        out
    }

    /// The session line (qtprogram's `sessionStep`): the step text, the
    /// progress (hidden at PowerCycle and once finished), Retry at PowerCycle,
    /// and Cancel -- "Give Up" at PowerCycle -- when the step is cancellable.
    fn session_view(&self) -> Value {
        let Some(s) = &self.session else {
            return json!({ "stepText": "", "progress": null, "retryVisible": false,
                "cancelVisible": false, "cancelText": "Cancel" });
        };
        let progress = if s.running && s.step != Step::PowerCycle {
            match s.progress {
                Some((done, total)) => json!({ "done": done, "total": total }),
                None => json!("busy"),
            }
        } else {
            Value::Null
        };
        json!({
            "stepText": s.step_text,
            "progress": progress,
            "retryVisible": s.running && s.step == Step::PowerCycle,
            "cancelVisible": s.cancellable(),
            "cancelText": if s.step == Step::PowerCycle { "Give Up" } else { "Cancel" },
        })
    }

    /// The whole view state, the contract's `state` event.
    ///
    /// Runs on the engine thread and changes nothing. Applies qtprogram's
    /// `updateButtons`: Program is enabled with no tooltip exactly when
    /// [`Station::can_program`] passes (its refusal is the tooltip); Retest needs
    /// an answering tag and an OK record; Stop shows for a tag in a deployed
    /// state and Erase for one in FINISHED or ABORTED. The outstanding prompt is
    /// included with its `id`.
    pub(crate) fn view_state(&self) -> Value {
        let running = self.session_running();
        let answers = self.attached() && self.info.is_some();
        let state = self
            .status
            .as_ref()
            .filter(|_| answers)
            .and_then(|s| TagState::try_from(s.state).ok());
        let stop_visible = state.is_some_and(|s| {
            !matches!(
                s,
                TagState::Idle
                    | TagState::Finished
                    | TagState::Aborted
                    | TagState::Test
                    | TagState::SReset
            )
        });
        let erase_visible = matches!(state, Some(TagState::Finished | TagState::Aborted));
        let program = self.can_program();
        let action = |enabled: bool| json!({ "enabled": enabled, "tooltip": "" });
        json!({
            "title": TITLE,
            "firmware": self.firmware_view(running),
            "tagType": self.tag_type_view(running),
            "checks": self.checks.iter().zip(CHECK_NAMES).map(|(c, name)| json!({
                "name": name, "level": c.level.name(), "text": c.text,
            })).collect::<Vec<_>>(),
            "rowButtons": {
                "recordChoose": self.level(Check::Record) != Level::Ok,
                "stop": { "visible": stop_visible, "enabled": !running },
                "erase": { "visible": erase_visible, "enabled": !running },
            },
            "tag": self.tag_view(running),
            "actions": {
                "program": { "enabled": program.is_ok(), "tooltip": program.err().unwrap_or_default() },
                "retest": action(!running && answers && self.level(Check::Record) == Level::Ok),
                "nextTag": action(!running),
                "attach": action(!running && self.level(Check::Programmer) == Level::Ok),
            },
            "session": self.session_view(),
            "batch": format!(
                "Batch: {} programmed this session, {} not OK",
                self.ok_count, self.failed_count
            ),
            "autoAttach": self.auto_attach(),
            "recordPath": self.record.as_ref().and_then(|r| r.path()).map(|p| p.to_string_lossy().into_owned()),
            "prompt": self.prompt.as_ref().map(|p| {
                let mut body = p.body.clone();
                body["id"] = json!(p.id);
                body
            }),
        })
    }
}

/// Unit tests for the view's text formatting.
#[cfg(test)]
mod tests {
    use super::clock_text;

    /// The clock offset reads as qtprogram printed it, in both formats.
    #[test]
    fn clock_follows_qtprogram_formats() {
        assert_eq!(clock_text(0.02), "+0.02 s");
        assert_eq!(clock_text(-3.456), "-3.46 s");
        assert_eq!(clock_text(12345.0), "+1.23e+04 s");
        assert_eq!(clock_text(-2000.0), "-2e+03 s");
    }
}
