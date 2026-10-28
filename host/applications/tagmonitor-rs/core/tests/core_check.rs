//! `qtmon_core_check`'s assertions (host/applications/qtmon/test/
//! qtmon_core_check.cc), against the Rust port, plus the menu bar against
//! test/menus.txt.

use std::fs;
use std::path::PathBuf;

use tagcore::UsbDev;
use tagcore::configshape::ConfigShape;
use tagcore::proto::{Config, Status, TagInfo, TagState, TagType, TestResult};
use tagmonitor_core::model::commands::Command;
use tagmonitor_core::model::configdocument::{
    ConfigDocument, DocumentSource, OnAttach, ParseFileError, on_attach,
};
use tagmonitor_core::model::menus::{
    MenuEntry, MenuItem, Shortcut, command_text, item_text, menu_bar,
};
use tagmonitor_core::model::monitormodel::{MonitorEvent, MonitorModel, NO_ANSWER_MS};
use tagmonitor_core::model::startchecks::{StartCheck, start_check_message};
use tagmonitor_core::model::tagrules::{describe_base, monitor_version_accepted};

/// A minimal configuration of `tag_type`.
fn config_of(tag_type: TagType) -> Config {
    Config {
        tag_type: tag_type as i32,
        ..Default::default()
    }
}

#[test]
fn config_document_rules() {
    let mut doc = ConfigDocument::default();
    assert!(!doc.has_document() && doc.tag_type() == TagType::TagUnspecified);
    doc.set_approved(true);
    assert!(!doc.approved(), "approval is ignored with no document");
    assert!(!doc.is_modified(None), "no document is never modified");

    let a = config_of(TagType::Uiuctag);
    doc.load(a.clone(), DocumentSource::Tag, "");
    doc.set_baseline(Some(&a));
    assert!(doc.has_document() && doc.tag_type() == TagType::Uiuctag);
    assert!(
        !doc.is_modified(Some(&a)),
        "the loaded values are not modified"
    );
    assert!(
        doc.is_modified(None),
        "values the form cannot send count as modified"
    );

    doc.set_approved(true);
    assert!(doc.approved());
    doc.content_changed();
    assert!(!doc.approved(), "a change to the values withdraws approval");

    doc.set_approved(true);
    let mut edited = a.clone();
    edited.start_delay = 5;
    assert!(doc.is_modified(Some(&edited)));
    doc.saved("/tmp/x.json", edited.clone());
    assert!(
        doc.approved(),
        "saving keeps approval: the values did not change"
    );
    assert!(doc.source() == DocumentSource::File && doc.path() == "/tmp/x.json");
    assert!(
        !doc.is_modified(Some(&edited)),
        "saved values are the new baseline"
    );

    doc.load(a.clone(), DocumentSource::Defaults, "");
    assert!(!doc.approved(), "loading withdraws approval");
    doc.set_baseline(None);
    assert!(
        doc.is_modified(Some(&a)),
        "with no baseline every state is modified"
    );

    doc.clear();
    assert!(!doc.has_document() && !doc.approved() && doc.path().is_empty());

    // Files: a saved file reads back as written, onto its type's defaults.
    let defaults = ConfigShape::default_config(TagType::Uiuctag).expect("UIUCTAG defaults");
    let text = ConfigDocument::file_text(&defaults);
    assert!(text.contains("\"tag_type\": \"UIUCTAG\""), "{text}");
    let back = ConfigDocument::parse_file(&text, TagType::TagUnspecified).expect("reads back");
    assert_eq!(back, defaults, "a saved file reads back as written");

    assert!(matches!(
        ConfigDocument::parse_file("{not json", TagType::TagUnspecified),
        Err(ParseFileError::Parse(_))
    ));
    assert!(ConfigDocument::names_no_tag_type("{}"));
    assert!(!ConfigDocument::names_no_tag_type(
        "{\"tag_type\":\"IMUTAG\"}"
    ));
    assert!(!ConfigDocument::names_no_tag_type("{not json"));
    let fallback = ConfigDocument::parse_file("{}", TagType::Imutag).expect("fallback");
    assert_eq!(fallback.tag_type(), TagType::Imutag);
    assert!(matches!(
        ConfigDocument::parse_file("{}", TagType::TagUnspecified),
        Err(ParseFileError::NoDefaults(_))
    ));
}

/// A status in `state` with the tag's clock at `millis`.
fn status_of(state: TagState, millis: i64, test: TestResult) -> Status {
    Status {
        state: state as i32,
        millis,
        test_status: test as i32,
        ..Default::default()
    }
}

#[test]
fn monitor_model_rules() {
    let t0: i64 = 1_800_000_000_000;
    let mut m = MonitorModel::new();
    assert!(!m.attached() && m.state() == TagState::StateUnspecified);
    let info = TagInfo {
        tag_type: TagType::Imutag as i32,
        uuid: "U1".into(),
        extflashsz: 1 << 20,
        ..Default::default()
    };
    m.set_attached(info);
    assert!(m.attached() && m.status().is_none());

    m.apply_status(
        status_of(TagState::Idle, t0 - 3000, TestResult::AllPassed),
        3.7,
        t0,
    );
    assert_eq!(m.clock_offset_s(), -3.0);
    assert_eq!(m.voltage(), 3.7);
    assert_eq!(m.state(), TagState::Idle);

    let mut piece = status_of(TagState::Idle, t0, TestResult::AllPassed);
    piece.debug_message = "first li".into();
    assert!(
        m.apply_status(piece.clone(), 0.0, t0).is_empty(),
        "an unfinished line waits"
    );
    piece.debug_message = "ne\r\nsecond\n".into();
    assert_eq!(
        m.apply_status(piece, 0.0, t0),
        vec![
            MonitorEvent::TagLogLine("first line".into()),
            MonitorEvent::TagLogLine("second".into())
        ]
    );

    m.test_sent(t0);
    assert!(m.test_pending());
    assert!(
        m.apply_status(
            status_of(TagState::Idle, t0, TestResult::AllPassed),
            0.0,
            t0 + 400
        )
        .is_empty()
    );
    assert!(
        m.test_pending(),
        "a result before the minimum is the previous run's"
    );
    assert_eq!(
        m.apply_status(
            status_of(TagState::Idle, t0, TestResult::RtcFailed),
            0.0,
            t0 + 1000
        ),
        vec![MonitorEvent::TestFinished(TestResult::RtcFailed)]
    );
    m.test_sent(t0);
    assert!(
        m.apply_status(
            status_of(TagState::Test, t0, TestResult::TestRunning),
            0.0,
            t0 + 5000
        )
        .is_empty()
    );
    assert_eq!(
        m.apply_status(
            status_of(TagState::Test, t0, TestResult::TestRunning),
            0.0,
            t0 + 60_001
        ),
        vec![MonitorEvent::TestTimedOut]
    );

    m.start_sent(t0, "LIBUSB timeout".into());
    assert!(
        m.apply_status(
            status_of(TagState::Idle, t0, TestResult::AllPassed),
            0.0,
            t0 + 1000
        )
        .is_empty()
    );
    assert_eq!(
        m.apply_status(
            status_of(TagState::Running, t0, TestResult::AllPassed),
            0.0,
            t0 + 2000
        ),
        vec![MonitorEvent::StartSucceeded {
            state: TagState::Running,
            message: "LIBUSB timeout".into()
        }]
    );
    m.start_sent(t0, String::new());
    assert_eq!(
        m.apply_status(
            status_of(TagState::Idle, t0, TestResult::AllPassed),
            0.0,
            t0 + 7000
        ),
        vec![MonitorEvent::StartFailed {
            state: TagState::Idle,
            message: String::new()
        }]
    );
    m.start_sent(t0, String::new());
    assert!(matches!(
        m.apply_status(
            status_of(TagState::Configured, t0, TestResult::AllPassed),
            0.0,
            t0 + 100
        )[..],
        [MonitorEvent::StartSucceeded { .. }]
    ));

    let mut finished = status_of(TagState::Finished, t0, TestResult::AllPassed);
    finished.external_data_count = 43;
    m.apply_status(finished, 0.0, t0);
    m.erase_sent();
    assert!(
        m.erasing() && m.erase_maximum() == 22,
        "an IMUTag's erase is sized from its pages"
    );
    let mut erasing = status_of(TagState::SReset, t0, TestResult::AllPassed);
    erasing.sectors_erased = 5;
    erasing.erase_sectors_total_plus_one = 31;
    assert!(m.apply_status(erasing.clone(), 0.0, t0).is_empty());
    assert_eq!((m.erase_value(), m.erase_maximum()), (5, 30));
    erasing.erase_sectors_total_plus_one = 11;
    m.apply_status(erasing, 0.0, t0);
    assert_eq!(m.erase_maximum(), 30, "the erase maximum never decreases");
    assert_eq!(
        m.apply_status(
            status_of(TagState::Idle, t0, TestResult::AllPassed),
            0.0,
            t0
        ),
        vec![MonitorEvent::EraseFinished]
    );

    assert_eq!(m.silence_ms(t0 + 500), 500);
    assert!(m.silence_ms(t0 + NO_ANSWER_MS) >= NO_ANSWER_MS);
    m.download_started();
    assert_eq!(
        m.silence_ms(t0 + 60_000),
        0,
        "a download pauses the polls on purpose"
    );
    m.test_sent(t0);
    let input = m.command_inputs(true, TagType::Imutag);
    assert!(input.attached && input.state == TagState::Idle && input.tag_type == TagType::Imutag);
    assert!(input.document && input.document_type == TagType::Imutag);
    assert!(input.downloading && input.test_pending);

    m.set_detached();
    assert!(!m.attached() && m.status().is_none() && !m.test_pending() && !m.downloading());
    assert_eq!(m.silence_ms(t0 + 60_000), 0, "a detached tag is not silent");
}

#[test]
fn window_rules() {
    assert_eq!(on_attach(DocumentSource::None), OnAttach::Replace);
    assert_eq!(on_attach(DocumentSource::Defaults), OnAttach::Keep);
    assert_eq!(on_attach(DocumentSource::File), OnAttach::Keep);
    assert_eq!(on_attach(DocumentSource::Tag), OnAttach::ReplaceIfDiscarded);

    assert!(monitor_version_accepted(2.1) && !monitor_version_accepted(1.9));
    let base = UsbDev {
        bus: 1,
        address: 6,
        vid: 0x483,
        pid: 0x3748,
        ..Default::default()
    };
    assert_eq!(describe_base(&base), "001:006    0x483:0x3748");

    assert_eq!(
        start_check_message(StartCheck::Clock, -30.0, TestResult::AllPassed),
        "Clock is -30 s off (limit 2 s)."
    );
    assert_eq!(
        start_check_message(StartCheck::Clock, -1.45e9, TestResult::AllPassed),
        "Clock is -1.45e+09 s off (limit 2 s)."
    );
    assert_eq!(
        start_check_message(StartCheck::SelfTest, 0.0, TestResult::RtcFailed),
        "Self-test is RTC_FAILED."
    );
    assert_eq!(
        start_check_message(StartCheck::Approval, 0.0, TestResult::AllPassed),
        "Configuration is not approved."
    );
}

/// The menu bar against the menu-bar part of test/menus.txt: each entry's
/// label, whether it has a shortcut, its tooltip, and the separators. Submenu
/// contents (tag types, log levels) are the UI's, not the table's.
#[test]
fn menu_bar_matches_qtmonitor() {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../qtmon/test/menus.txt");
    let text = fs::read_to_string(&path).expect("menus.txt");
    let mut expected: Vec<String> = Vec::new();
    for line in text.lines().take_while(|l| !l.starts_with("popup ")) {
        if let Some(title) = line.strip_prefix("menu ") {
            expected.push(format!("menu {title}"));
        } else if let Some(entry) = line.strip_prefix("  ")
            && !entry.starts_with(' ')
        {
            // label [shortcut]  -- tooltip  (checked)
            let label = entry.split("  ").next().unwrap_or("");
            let shortcut = entry.contains("  [");
            let tooltip = entry.split("  -- ").nth(1).unwrap_or("");
            expected.push(format!("{label}|{shortcut}|{tooltip}"));
        }
    }

    let mut actual: Vec<String> = Vec::new();
    for menu in menu_bar() {
        actual.push(format!("menu {}", menu.title));
        for entry in &menu.entries {
            let t = match entry {
                MenuEntry::Separator => {
                    actual.push("---|false|".into());
                    continue;
                }
                MenuEntry::Command(c) => command_text(*c),
                MenuEntry::Item(i) => item_text(*i),
            };
            actual.push(format!(
                "{}|{}|{}",
                t.label,
                t.shortcut != Shortcut::None,
                t.tooltip
            ));
        }
    }
    assert_eq!(actual, expected);

    // Every command is somewhere in the menu bar.
    for c in tagmonitor_core::model::commands::ALL_COMMANDS {
        assert!(
            menu_bar()
                .iter()
                .any(|m| m.entries.contains(&MenuEntry::Command(c))),
            "{c:?} is in no menu"
        );
    }
    let _ = (Command::Attach, MenuItem::Quit);
}
