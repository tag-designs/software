//! qtmonitor's configuration probe, replayed through the Rust form.
//!
//! Every case in `host/applications/qtmon/test/cases/` is run as
//! `configprobe.cpp`'s `runOnce()` runs it -- a fresh editor, the case's
//! document (or New), the tag's state, then any file opened -- and the
//! result is compared with the golden from the reference qtmonitor build:
//!
//! - `sent_hex`: the bytes Start would send;
//! - `get_config_ok`, `document`, `layout`, `dialogs`, and `open` (the
//!   result; not its detail, which is the protobuf parser's own wording);
//! - `ui`: every radio, check box, spin box and date box, with its value,
//!   range, step, decimals, enabled state and tooltip. Group titles and
//!   labels are left out: no load changes them.
//!
//! Each failure names the case and key; the test fails if any differs.

use std::collections::BTreeMap;
use std::fs;
use std::path::PathBuf;

use prost::Message;
use qtmon_core::model::configdocument::DocumentSource;
use qtmon_core::model::configeditor::{ConfigEditor, OpenResult};
use serde_json::Value;
use tagcore::proto::{self, Config, TagState};

/// The probe's directory in the Qt app.
fn probe_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../qtmon/test")
}

/// A JSON file.
fn read(path: PathBuf) -> Value {
    serde_json::from_str(&fs::read_to_string(&path).unwrap()).unwrap()
}

/// What the probe records for one case, as the golden's fields.
struct Run {
    /// `"Tag PRESTAG modified=0"`.
    document: String,
    /// GetConfig() returned true.
    ok: bool,
    /// The serialised bytes, as hex.
    sent_hex: String,
    /// Panels shown, then the tab.
    layout: Vec<String>,
    /// Messages raised.
    dialogs: Vec<String>,
    /// `Opened`, `ParseError`, `NoDefaults`, or empty.
    open: String,
    /// The controls' fingerprint.
    ui: Vec<String>,
}

/// Runs one case.
fn run(case: &Value) -> Run {
    let config_text = case["config"].to_string();
    let config: Config = proto::from_json(&config_text).expect("case config parses");
    let now = case["now"].as_i64().unwrap();
    let state = TagState::from_str_name(case["state"].as_str().unwrap_or("IDLE")).unwrap();
    let mut editor = ConfigEditor::new();
    let mut dialogs = if case["new"].as_bool().unwrap_or(false) {
        editor
            .load_defaults(config.tag_type(), now)
            .unwrap_or_default()
    } else {
        editor.set_config(config.clone(), now)
    };
    if case["attached"].as_bool().unwrap_or(true) {
        editor.state_update(state);
    }
    let mut open = String::new();
    if let Some(text) = case["open"].as_str() {
        let (result, more) = editor.open_text(text, config.tag_type(), "", now);
        dialogs.extend(more);
        open = match result {
            OpenResult::Opened => "Opened",
            OpenResult::ParseError(_) => "ParseError",
            OpenResult::NoDefaults(_) => "NoDefaults",
        }
        .into();
    }
    let source = match editor.document().source() {
        DocumentSource::None => "None",
        DocumentSource::Defaults => "Defaults",
        DocumentSource::File => "File",
        DocumentSource::Tag => "Tag",
    };
    let document = format!(
        "{source} {} modified={}",
        editor.document().tag_type().as_str_name(),
        u8::from(editor.is_modified(now))
    );
    let sent = editor.get_config(now);
    let ok = sent.is_some();
    let bytes = sent.unwrap_or_default().encode_to_vec();
    let mut layout: Vec<String> = editor
        .form()
        .shown_names()
        .iter()
        .map(|n| format!("panel {n}"))
        .collect();
    layout.push("configtab visible=1 enabled=1".into());
    Run {
        document,
        ok,
        sent_hex: bytes.iter().map(|b| format!("{b:02x}")).collect(),
        layout,
        dialogs,
        open,
        ui: editor.form().fingerprint(),
    }
}

/// The golden's control lines: those for controls a load sets.
fn golden_controls(result: &Value) -> Vec<String> {
    let mut lines: Vec<String> = result["ui"]
        .as_array()
        .unwrap()
        .iter()
        .filter_map(Value::as_str)
        .filter(|l| {
            let kind = l
                .split(" | ")
                .nth(1)
                .unwrap_or("")
                .split(' ')
                .next()
                .unwrap_or("");
            matches!(kind, "radio" | "check" | "double" | "int" | "datetime")
        })
        .map(String::from)
        .collect();
    lines.sort();
    lines
}

/// Strings of a JSON array, or none.
fn strings(v: &Value) -> Vec<String> {
    v.as_array()
        .map(|a| {
            a.iter()
                .filter_map(Value::as_str)
                .map(String::from)
                .collect()
        })
        .unwrap_or_default()
}

/// Every case against its golden.
#[test]
fn every_probe_case_matches_the_golden() {
    let mut failures: BTreeMap<String, Vec<String>> = BTreeMap::new();
    let mut cases = 0;
    let mut names: Vec<_> = fs::read_dir(probe_dir().join("cases"))
        .unwrap()
        .map(|e| e.unwrap().path())
        .collect();
    names.sort();
    for path in names {
        let file = path.file_name().unwrap().to_string_lossy().into_owned();
        let case_list = read(path.clone());
        let golden = read(probe_dir().join("golden").join(&file));
        let results = golden["results"].as_array().unwrap();
        for case in case_list["cases"].as_array().unwrap() {
            cases += 1;
            let id = case["id"].as_str().unwrap();
            let want = results
                .iter()
                .find(|r| r["id"] == id)
                .expect("golden for case");
            let got = run(case);
            let mut fail = |key: &str, detail: String| {
                failures
                    .entry(key.into())
                    .or_default()
                    .push(format!("{file} {id}: {detail}"));
            };
            if want["sent_hex"].as_str().unwrap() != got.sent_hex {
                fail(
                    "sent_hex",
                    format!("want {} got {}", want["sent_hex"], got.sent_hex),
                );
            }
            if want["get_config_ok"].as_bool().unwrap() != got.ok {
                fail("get_config_ok", format!("got {}", got.ok));
            }
            if want["document"].as_str().unwrap() != got.document {
                fail(
                    "document",
                    format!("want {} got {}", want["document"], got.document),
                );
            }
            if strings(&want["layout"]) != got.layout {
                fail(
                    "layout",
                    format!("want {:?} got {:?}", strings(&want["layout"]), got.layout),
                );
            }
            if strings(&want["dialogs"]) != got.dialogs {
                fail(
                    "dialogs",
                    format!("want {:?} got {:?}", strings(&want["dialogs"]), got.dialogs),
                );
            }
            if want["open"].as_str().unwrap_or("") != got.open {
                fail("open", format!("want {} got {}", want["open"], got.open));
            }
            let want_ui = golden_controls(want);
            if want_ui != got.ui {
                let missing: Vec<_> = want_ui.iter().filter(|l| !got.ui.contains(l)).collect();
                let extra: Vec<_> = got.ui.iter().filter(|l| !want_ui.contains(l)).collect();
                fail("ui", format!("want {missing:#?}\n got {extra:#?}"));
            }
        }
    }
    for (key, list) in &failures {
        eprintln!("== {key}: {} cases", list.len());
        for f in list.iter().take(4) {
            eprintln!("  {f}");
        }
    }
    assert!(
        failures.is_empty(),
        "{} of {cases} cases differ",
        failures.values().map(Vec::len).sum::<usize>()
    );
    eprintln!("{cases} cases match");
}
