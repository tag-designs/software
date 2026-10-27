//! The view state: everything the window shows, as the contract's `state`
//! event, with qtmonitor's display rules (`MainWindow::render`,
//! `populateTagInfo`, `updateTitle`, `updateActions` and `TagStatusStrip`)
//! applied here, so the UI only draws.
//!
//! [`Monitor::view_state`] reads the monitor and never changes it; it reads
//! no clock and does no I/O, so the loop can compare its result with the
//! last one sent. The JSON's shape is in `design/ui-engine-contract.md`.

use serde_json::{Map, Value, json};
use tagcore::configshape::ConfigShape;
use tagcore::proto::{TagInfo, TestResult};
use tagcore::textformat::format_g;

use super::monitor::{Monitor, PromptKind, VERSION, file_name};
use crate::model::commands::{ALL_COMMANDS, Availability, command_name};
use crate::model::menus::{
    EntryText, MenuEntry, MenuItem, Shortcut, command_text, item_text, menu_bar,
};
use crate::model::startchecks::CLOCK_TOLERANCE_S;
use crate::model::tagrules::{describe_base, is_downloadable_state};

/// The clock offset as the status strip prints it: `%+.2f s` under 1000 s,
/// else `%+.3g s` (an unset clock is about `-1.45e+09 s`).
pub(super) fn clock_text(offset: f64) -> String {
    if offset.abs() < 1000.0 {
        return format!("{offset:+.2} s");
    }
    let sign = if offset >= 0.0 { "+" } else { "" };
    format!("{sign}{} s", format_g(offset, 3, 3, |_| true))
}

/// `{ text, warn }`: a status strip value, red when `warn`.
fn item(text: impl Into<String>, warn: bool) -> Value {
    json!({ "text": text.into(), "warn": warn })
}

/// A shortcut's name for the UI: `"open"`, `"save"`, `"quit"` or `null`.
fn shortcut_name(s: Shortcut) -> Value {
    match s {
        Shortcut::None => Value::Null,
        Shortcut::Open => json!("open"),
        Shortcut::Save => json!("save"),
        Shortcut::Quit => json!("quit"),
    }
}

/// One menu entry's label, tooltip and shortcut, with `"&"` mnemonics
/// removed (a web menu has none).
fn entry_text(text: EntryText) -> Map<String, Value> {
    let mut m = Map::new();
    m.insert("label".into(), json!(text.label.replace('&', "")));
    m.insert("tooltip".into(), json!(text.tooltip));
    m.insert("shortcut".into(), shortcut_name(text.shortcut));
    m
}

/// The id the UI uses for an always-available item.
fn item_id(item: MenuItem) -> &'static str {
    match item {
        MenuItem::NewConfiguration => "new_configuration",
        MenuItem::OpenRecent => "open_recent",
        MenuItem::SaveErrorLog => "save_error_log",
        MenuItem::ClearErrorLog => "clear_error_log",
        MenuItem::LogLevel => "log_level",
        MenuItem::Quit => "quit",
        MenuItem::UserGuide => "user_guide",
        MenuItem::About => "about",
    }
}

/// The `menus` query: the menu bar from the core table, with the New
/// Configuration tag types. Which commands are enabled is in the view
/// state's `actions`; the recent files are in its `recent`.
pub(super) fn menus_json() -> Value {
    let menus: Vec<Value> = menu_bar()
        .into_iter()
        .map(|menu| {
            let entries: Vec<Value> = menu
                .entries
                .into_iter()
                .map(|entry| match entry {
                    MenuEntry::Separator => json!({ "kind": "separator" }),
                    MenuEntry::Command(c) => {
                        let mut m = entry_text(command_text(c));
                        m.insert("kind".into(), json!("command"));
                        m.insert("id".into(), json!(command_name(c)));
                        Value::Object(m)
                    }
                    MenuEntry::Item(i) => {
                        let mut m = entry_text(item_text(i));
                        m.insert("kind".into(), json!("item"));
                        m.insert("id".into(), json!(item_id(i)));
                        Value::Object(m)
                    }
                })
                .collect();
            json!({ "title": menu.title.replace('&', ""), "entries": entries })
        })
        .collect();
    let tag_types: Vec<&str> = ConfigShape::tag_types()
        .iter()
        .map(|t| t.as_str_name())
        .collect();
    json!({ "menus": menus, "newConfigurationTypes": tag_types })
}

/// The Tag page's identity fields, as `populateTagInfo` filled them.
fn info_json(info: &TagInfo) -> Value {
    json!({
        "tagType": info.tag_type().as_str_name(),
        "board": info.board_desc,
        "firmware": info.firmware,
        "gitHash": info.githash,
        "gitUrl": info.gitrepo,
        "uuid": info.uuid,
        "flash": format!("{}KB", info.intflashsz),
        "flashExt": format!("{}MB", info.extflashsz / (1024 * 1024)),
        "buildDate": info.build_time,
        "sourcePath": info.source_path,
    })
}

impl Monitor {
    /// The status strip (TagStatusStrip): blank values when detached.
    fn strip_json(&self) -> Value {
        let m = &self.model;
        let blank = || item("", false);
        let mut strip = json!({
            "attached": m.attached(),
            "type": "",
            "typeTooltip": "Attached tag type and board UUID",
            "state": blank(),
            "clock": blank(),
            "voltage": "",
            "test": blank(),
            "approved": self.editor.approved(),
            "approvalEnabled": self.editor.document().has_document(),
        });
        if !m.attached() {
            return strip;
        }
        let uuid = &m.info().uuid;
        strip["type"] = json!(m.tag_type().as_str_name());
        strip["typeTooltip"] = json!(if uuid.is_empty() {
            "Attached tag type".to_string()
        } else {
            format!("Attached tag type; board UUID {uuid}")
        });
        if m.status().is_some() {
            let state = m.state().as_str_name();
            strip["state"] = if self.silence_s > 0 {
                item(format!("{state} (no answer {} s)", self.silence_s), true)
            } else {
                item(state, false)
            };
            let offset = m.clock_offset_s();
            strip["clock"] = item(clock_text(offset), offset.abs() > CLOCK_TOLERANCE_S);
            strip["voltage"] = json!(format!("{:.2} V", m.voltage()));
            strip["test"] = if m.test_pending() {
                item("Running", false)
            } else {
                item(
                    m.last_test().as_str_name(),
                    m.last_test() != TestResult::AllPassed,
                )
            };
        }
        strip
    }

    /// The Tag page: identity, log counts and whether Download is offered.
    fn tag_json(&self) -> Value {
        let m = &self.model;
        let (internal, external, downloadable) = match m.status() {
            Some(s) if m.attached() => (
                json!(s.internal_data_count.to_string()),
                if s.external_data_count != 0 {
                    json!(s.external_data_count.to_string())
                } else {
                    Value::Null
                },
                is_downloadable_state(s.state()) && !m.downloading(),
            ),
            _ => (json!(""), Value::Null, false),
        };
        json!({
            "enabled": m.attached(),
            "info": self.shown_info.as_ref().map(info_json),
            "internalCount": internal,
            "externalCount": external,
            "downloadEnabled": downloadable,
        })
    }

    /// The Configuration tab: the document and the form's shown panels.
    fn config_json(&self) -> Value {
        let doc = self.editor.document();
        json!({
            "document": doc.has_document(),
            "name": self.document_name(),
            "tagType": doc.tag_type().as_str_name(),
            "modified": self.is_modified(),
            "panels": if doc.has_document() { self.editor.form().view() } else { json!([]) },
        })
    }

    /// The shown prompt, or `null`.
    fn prompt_json(&self) -> Value {
        let Some(p) = self.prompts.front() else {
            return Value::Null;
        };
        let mut v = match &p.kind {
            PromptKind::Alert {
                title,
                text,
                informative,
                detail,
                style,
            } => json!({
                "kind": "alert", "title": title, "text": text,
                "informative": informative, "detail": detail, "style": style,
            }),
            PromptKind::ChooseBase(bases) => json!({
                "kind": "chooseBase",
                "title": "Available Bases",
                "label": "Please select a base",
                "items": bases.iter().map(describe_base).collect::<Vec<_>>(),
            }),
            PromptKind::StartAnyway(failures) => json!({
                "kind": "startAnyway",
                "title": "Start Tag",
                "text": "Start the tag anyway?",
                "failures": failures,
            }),
            PromptKind::ConfirmErase => json!({
                "kind": "confirmErase",
                "title": "Reset Tag",
                "text": "Erase tag state and data ?",
            }),
            PromptKind::Unsaved(_) => json!({
                "kind": "unsaved",
                "title": "Unsaved Configuration",
                "text": format!("The configuration \"{}\" has unsaved changes.", self.document_name()),
                "informative": "Save them before continuing?",
            }),
            PromptKind::OpenConfig => json!({
                "kind": "openFile",
                "title": "Open Configuration",
                "start": self.open_start_dir(),
                "filters": [{ "name": "Configuration", "extensions": ["json"] }],
            }),
            PromptKind::SaveConfig(_) => json!({
                "kind": "saveFile",
                "title": "Save Configuration",
                "suggested": self.suggested_config_path(),
                "filters": [{ "name": "Configuration", "extensions": ["json"] }],
            }),
            PromptKind::SaveDownload => json!({
                "kind": "saveFile",
                "title": "Save File",
                "suggested": self.config.home_dir.join(super::download::DEFAULT_DOWNLOAD_NAME).to_string_lossy(),
                "filters": [{ "name": "SQLite database", "extensions": ["db3"] }],
            }),
            PromptKind::ChooseTagType(path, types) => json!({
                "kind": "chooseTagType",
                "title": "Open Configuration",
                "label": format!("{} does not say which tag type it is for. Open it as:", file_name(path)),
                "items": types.iter().map(|t| t.as_str_name()).collect::<Vec<_>>(),
            }),
        };
        v["id"] = json!(p.id);
        v
    }

    /// The whole view state.
    pub(crate) fn view_state(&self) -> Value {
        let mut title = format!("Tag Monitor v{VERSION}");
        let name = self.document_name();
        if !name.is_empty() {
            title += &format!(" \u{2014} {name}");
        }
        let actions: Map<String, Value> = ALL_COMMANDS
            .iter()
            .map(|&c| {
                let a = self.available(c);
                (
                    command_name(c).to_string(),
                    json!({ "visible": a != Availability::Hidden, "enabled": a == Availability::Enabled }),
                )
            })
            .collect();
        let erase = (self.erase_shown && self.model.erasing()).then(|| {
            json!({
                "value": self.model.erase_value(),
                "max": self.model.erase_maximum(),
                "text": if self.silence_s > 0 {
                    format!("Erasing flash... (no answer from the tag for {} s)", self.silence_s)
                } else {
                    "Erasing flash...".to_string()
                },
            })
        });
        let download = self
            .download
            .map(|(value, max)| json!({ "value": value, "max": max }));
        json!({
            "title": title,
            "modified": self.is_modified(),
            "attaching": self.attaching,
            "strip": self.strip_json(),
            "tag": self.tag_json(),
            "config": self.config_json(),
            "actions": actions,
            "recent": self.recent.iter().map(|p| json!({ "path": p, "name": file_name(p) })).collect::<Vec<_>>(),
            "erase": erase,
            "download": download,
            "prompt": self.prompt_json(),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// TagStatusStrip's two clock formats.
    #[test]
    fn clock_text_matches_the_status_strip() {
        assert_eq!(clock_text(0.0), "+0.00 s");
        assert_eq!(clock_text(-30.0), "-30.00 s");
        assert_eq!(clock_text(1.234), "+1.23 s");
        assert_eq!(clock_text(-1.45e9), "-1.45e+09 s");
        assert_eq!(clock_text(2.5e3), "+2.5e+03 s");
    }
}
