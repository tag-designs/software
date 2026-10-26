//! The station: qtprogram's `Station` (`station.cpp`), ported.
//!
//! Eight checks gate programming; the base and the tag are found, attached
//! and polled; the firmware, tag type and record are chosen; prompts ask the
//! UI for decisions. Texts and rules are qtprogram's except where the
//! contract says otherwise (probe-rs instead of STM32_Programmer_CLI).

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::sync::mpsc::Sender;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use log::{info, warn};
use serde_json::{Value, json};
use tagcore::proto::{Status, TagInfo, TagState, TestResult};
use tagcore::{Tag, UsbDev};
use tagprobe::{Connect, DEV_L432, DEV_U375, Driver};

use super::session::SessionState;
use super::{EngineConfig, Msg, query_error, set_log_level};
use crate::model::catalog::Catalog;
use crate::model::flashplan::Erase;
use crate::model::image::{FirmwareImage, load_firmware_image};
use crate::model::record::RecordLog;
use crate::model::settings::{self as key, Settings};

const L432_MAX_VOLTS: f32 = 3.6;
const LOW_VOLTS: f32 = 1.8;
const MISSED_POLLS: u32 = 5;
const ATTACH_TRIES: u32 = 10;
const ATTACH_RETRY: Duration = Duration::from_millis(200);
pub(crate) const CLOCK_TOLERANCE_S: f64 = 2.0;
const BASE_POLL: Duration = Duration::from_millis(1000);
const STATUS_POLL: Duration = Duration::from_millis(400);

/// The station checks, in display order.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Check {
    Programmer = 0,
    Base,
    Swd,
    Supply,
    Tag,
    TagState,
    Firmware,
    Record,
}

pub(crate) const CHECK_NAMES: [&str; 8] = [
    "Programmer",
    "Base",
    "SWD",
    "Supply",
    "Tag",
    "Tag state",
    "Firmware",
    "Record",
];

/// A check's level.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Level {
    Pending,
    Ok,
    Warn,
    Fail,
}

impl Level {
    pub(crate) fn name(self) -> &'static str {
        match self {
            Level::Pending => "pending",
            Level::Ok => "ok",
            Level::Warn => "warn",
            Level::Fail => "fail",
        }
    }
}

/// One check's state.
#[derive(Debug, Clone)]
pub(crate) struct CheckStatus {
    pub level: Level,
    pub text: String,
}

/// A question for the UI (`state.prompt`).
#[derive(Debug, Clone)]
pub(crate) struct Prompt {
    pub id: u64,
    pub body: Value,
}

/// Station state, owned by the engine thread.
pub(crate) struct Station {
    pub(crate) cfg: EngineConfig,
    tx: Sender<Msg>,
    pub(crate) settings: Settings,
    pub(crate) catalog: Arc<Catalog>,
    pub(crate) refreshing: bool,
    pub(crate) refresh_error: Option<String>,
    pub(crate) fetching: Option<String>,
    pub(crate) fetch_errors: HashMap<String, String>,

    pub(crate) checks: Vec<CheckStatus>,
    pub(crate) base: Option<UsbDev>,
    pub(crate) base_identity: Option<String>,
    pub(crate) have_base: bool,
    pub(crate) connect: Option<Connect>,
    pub(crate) tag: Tag,
    pub(crate) info: Option<TagInfo>,
    pub(crate) status: Option<Status>,
    pub(crate) offset: Option<f64>,
    missed: u32,
    attach_retry: Option<(Instant, u32)>,
    base_poll: Option<Instant>,
    status_poll: Option<Instant>,
    /// A worker owns the base (connect check, programming, read-back).
    pub(crate) worker_busy: bool,

    pub(crate) image: Option<FirmwareImage>,
    pub(crate) target: String,
    pub(crate) target_detected: bool,
    pub(crate) record: Option<RecordLog>,
    pub(crate) last_label: String,
    pub(crate) ok_count: u32,
    pub(crate) failed_count: u32,
    pub(crate) result: (String, Option<bool>),

    pub(crate) session: Option<SessionState>,
    pub(crate) prompt: Option<Prompt>,
    prompt_seq: u64,
    started: bool,
}

/// Milliseconds since the Unix epoch, now.
pub(crate) fn now_ms() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

/// The protobuf name of a tag state, as `TagState_Name` gives it.
pub(crate) fn state_name(state: i32) -> String {
    TagState::try_from(state)
        .map(|s| s.as_str_name().to_owned())
        .unwrap_or_else(|_| state.to_string())
}

/// The protobuf name of a test result, as `TestResult_Name` gives it.
pub(crate) fn test_name(result: i32) -> String {
    TestResult::try_from(result)
        .map(|s| s.as_str_name().to_owned())
        .unwrap_or_else(|_| result.to_string())
}

/// The family an image's flash page size implies.
pub(crate) fn mcu_for_page(page_size: u32) -> &'static str {
    if page_size == 4096 {
        "STM32U375"
    } else {
        "STM32L432"
    }
}

fn mcu_for_device(device_id: u16) -> &'static str {
    match device_id {
        DEV_U375 => "STM32U375",
        DEV_L432 => "STM32L432",
        _ => "unknown part",
    }
}

/// The value of one `key=` field of a base identity string.
pub(crate) fn identity_field<'a>(identity: &'a str, key: &str) -> Option<&'a str> {
    identity
        .split(';')
        .find_map(|kv| kv.strip_prefix(key)?.strip_prefix('='))
}

impl Station {
    pub(crate) fn new(cfg: EngineConfig, tx: Sender<Msg>) -> Self {
        let settings = Settings::load(&cfg.settings_path);
        let catalog = Arc::new(Catalog::new(&cfg.cache_dir));
        let last_label = settings.string(key::LAST_LABEL);
        let target = settings.string(key::TARGET);
        Self {
            cfg,
            tx,
            settings,
            catalog,
            refreshing: false,
            refresh_error: None,
            fetching: None,
            fetch_errors: HashMap::new(),
            checks: vec![
                CheckStatus {
                    level: Level::Pending,
                    text: String::new()
                };
                8
            ],
            base: None,
            base_identity: None,
            have_base: false,
            connect: None,
            tag: Tag::new(),
            info: None,
            status: None,
            offset: None,
            missed: 0,
            attach_retry: None,
            base_poll: None,
            status_poll: None,
            worker_busy: false,
            image: None,
            target,
            target_detected: false,
            record: None,
            last_label,
            ok_count: 0,
            failed_count: 0,
            result: (String::new(), None),
            session: None,
            prompt: None,
            prompt_seq: 0,
            started: false,
        }
    }

    // ---- infrastructure ----------------------------------------------

    pub(crate) fn set(&mut self, check: Check, level: Level, text: impl Into<String>) {
        self.checks[check as usize] = CheckStatus {
            level,
            text: text.into(),
        };
    }

    pub(crate) fn level(&self, check: Check) -> Level {
        self.checks[check as usize].level
    }

    pub(crate) fn text(&self, check: Check) -> &str {
        &self.checks[check as usize].text
    }

    /// Runs `work` on a worker thread and applies its result on the engine
    /// thread with `apply`.
    pub(crate) fn spawn_work<T: Send + 'static>(
        &self,
        work: impl FnOnce() -> T + Send + 'static,
        apply: impl FnOnce(&mut Station, T) + Send + 'static,
    ) {
        let tx = self.tx.clone();
        std::thread::spawn(move || {
            let result = work();
            let _ = tx.send(Msg::Done(Box::new(move |s: &mut Station| apply(s, result))));
        });
    }

    /// A sender for progress messages from a worker.
    pub(crate) fn sender(&self) -> Sender<Msg> {
        self.tx.clone()
    }

    pub(crate) fn raise(&mut self, body: Value) {
        self.prompt_seq += 1;
        self.prompt = Some(Prompt {
            id: self.prompt_seq,
            body,
        });
    }

    pub(crate) fn alert(&mut self, title: &str, text: &str) {
        self.raise(json!({ "kind": "alert", "title": title, "text": text, "style": "warning" }));
    }

    pub(crate) fn next_deadline(&self) -> Option<Instant> {
        [
            self.base_poll,
            self.status_poll,
            self.attach_retry.map(|(t, _)| t),
            self.session.as_ref().and_then(|s| s.tick_at),
        ]
        .into_iter()
        .flatten()
        .min()
    }

    pub(crate) fn fire_timers(&mut self, now: Instant) {
        if self.base_poll.is_some_and(|t| t <= now) {
            self.base_poll = Some(now + BASE_POLL);
            self.check_base();
        }
        if self.status_poll.is_some_and(|t| t <= now) {
            self.status_poll = Some(now + STATUS_POLL);
            self.poll();
        }
        if let Some((t, tries)) = self.attach_retry
            && t <= now
        {
            self.attach_retry = None;
            self.attach_tag(tries);
        }
        if self
            .session
            .as_ref()
            .and_then(|s| s.tick_at)
            .is_some_and(|t| t <= now)
        {
            if let Some(s) = self.session.as_mut() {
                s.tick_at = Some(now + Duration::from_secs(1));
            }
            self.session_tick();
        }
    }

    pub(crate) fn session_running(&self) -> bool {
        self.session.as_ref().is_some_and(|s| s.running)
    }

    pub(crate) fn attached(&self) -> bool {
        self.tag.is_attached()
    }

    fn busy(&self) -> bool {
        self.session_running() || self.worker_busy || self.attach_retry.is_some()
    }

    // ---- commands and queries ----------------------------------------

    pub(crate) fn command(&mut self, cmd: &str, args: &Value) {
        let s = |k: &str| args.get(k).and_then(Value::as_str).unwrap_or("").to_owned();
        let b = |k: &str| args.get(k).and_then(Value::as_bool).unwrap_or(false);
        match cmd {
            "start" => self.begin(),
            "choose_firmware" => {
                let id = s("id");
                if let Some(tag) = id.strip_prefix("release:") {
                    self.choose_release(tag);
                } else if let Some(path) = id.strip_prefix("file:") {
                    self.choose_file(path);
                }
            }
            "choose_file" => self.choose_file(&s("path")),
            "choose_target" => self.choose_target(&s("name")),
            "refresh_releases" => self.refresh_catalog(),
            "attach" => self.attach(),
            "next_tag" => self.next_tag(),
            "stop_tag" => self.stop_tag(),
            "erase_tag" => self.erase_tag(),
            "sync_clock" => self.sync_clock(),
            "program" => self.program(&s("label")),
            "retest" => self.retest(&s("label")),
            "retry_connect" => self.session_retry_connect(),
            "cancel_session" => self.session_cancel(),
            "set_auto_attach" => self.set_auto_attach(b("on")),
            "answer_auto_attach" => {
                self.prompt = None;
                self.set_auto_attach(b("on"));
                self.begin_checks();
            }
            "list_bases" => self.list_bases(),
            "choose_base" => {
                self.prompt = None;
                let index = args.get("index").and_then(Value::as_u64).unwrap_or(0) as usize;
                self.choose_base(index);
            }
            "choose_record" => self.raise_record_prompt(),
            "record_use_default" => {
                self.prompt = None;
                let path = self.default_record_path();
                if path.exists() {
                    self.open_record(&path)
                } else {
                    self.create_record(&path)
                }
            }
            "record_open" => {
                self.prompt = None;
                self.open_record(Path::new(&s("path")));
            }
            "record_create" => {
                self.prompt = None;
                let path = PathBuf::from(s("path"));
                if path.exists() {
                    self.open_record(&path)
                } else {
                    self.create_record(&path)
                }
            }
            "prompt_cancel" => self.prompt = None,
            "set_log_level" => set_log_level(&s("level")),
            "save_text" => {
                if let Err(e) = std::fs::write(s("path"), s("text")) {
                    self.raise(json!({ "kind": "alert", "title": "Error", "text": format!("Cannot open file: {e}"), "style": "warning" }));
                }
            }
            other => warn!("unknown command {other}"),
        }
    }

    pub(crate) fn query(&mut self, name: &str, args: &Value) -> Value {
        match name {
            "program_preview" => self.program_preview(),
            "label_notes" => {
                json!(self.label_notes(args.get("label").and_then(Value::as_str).unwrap_or("")))
            }
            "retest_default_label" => json!(self.retest_default_label()),
            "about" => json!({
                "title": "About Tag Programmer",
                "text": "Tag Programmer v2.0\n\nProgram and test a batch of tags through a USB base, recording each one."
            }),
            "user_guide_url" => json!(self.cfg.user_guide_url),
            _ => query_error("unknown query"),
        }
    }

    // ---- start-up ----------------------------------------------------

    fn begin(&mut self) {
        if self.started {
            return;
        }
        self.started = true;
        if !self.settings.contains(key::AUTO_ATTACH) {
            self.raise(json!({ "kind": "autoAttach" }));
            return;
        }
        self.begin_checks();
    }

    fn begin_checks(&mut self) {
        self.set(
            Check::Programmer,
            Level::Ok,
            format!("probe-rs {}", tagprobe::PROBE_RS_VERSION),
        );
        self.check_base();
        self.refresh_catalog();
        self.load_image();
        let path = self.settings.string(key::RECORD_PATH);
        if !path.is_empty() {
            let mut record = RecordLog::new();
            match record.open(Path::new(&path)) {
                Ok(()) => self.record = Some(record),
                Err(e) => warn!("{e}"),
            }
        }
        self.check_record();
        if self.record.is_none() {
            self.raise_record_prompt();
        }
    }

    pub(crate) fn auto_attach(&self) -> bool {
        self.settings.bool(key::AUTO_ATTACH, false)
    }

    fn set_auto_attach(&mut self, on: bool) {
        self.save_setting(key::AUTO_ATTACH, on);
        if on && self.have_base && !self.attached() && self.started {
            self.attach();
        }
    }

    // ---- base --------------------------------------------------------

    fn check_base(&mut self) {
        if self.busy() || self.attached() {
            return;
        }
        let bases = tagprobe::list();
        if bases.is_empty() {
            self.have_base = false;
            self.set(
                Check::Base,
                Level::Warn,
                "No base on USB. Plug in the base.",
            );
            if self.base_poll.is_none() {
                self.base_poll = Some(Instant::now() + BASE_POLL);
            }
            return;
        }
        self.base_poll = None;
        let saved = self.settings.string(key::BASE_SERIAL);
        if bases.len() == 1 {
            self.use_base(bases[0].clone());
        } else if let Some(b) = bases
            .iter()
            .find(|b| !saved.is_empty() && b.serial == saved)
        {
            self.use_base(b.clone());
        } else {
            self.set(
                Check::Base,
                Level::Warn,
                format!("{} bases on USB: choose one.", bases.len()),
            );
            self.raise_base_prompt(&bases);
        }
    }

    fn raise_base_prompt(&mut self, bases: &[UsbDev]) {
        let saved = self.settings.string(key::BASE_SERIAL);
        let items: Vec<String> = bases
            .iter()
            .map(|b| {
                let serial = if b.serial.is_empty() {
                    "serial unknown"
                } else {
                    &b.serial
                };
                format!("ST-LINK {serial}  (bus {}, address {})", b.bus, b.address)
            })
            .collect();
        let current = bases.iter().position(|b| b.serial == saved).unwrap_or(0);
        let label = if bases.len() > 1 {
            "Several bases are connected. Use:"
        } else {
            "Use:"
        };
        self.raise(
            json!({ "kind": "chooseBase", "label": label, "items": items, "current": current }),
        );
    }

    fn list_bases(&mut self) {
        if self.busy() {
            self.raise(json!({ "kind": "alert", "title": "Choose a Base",
                "text": "No ST-LINK is on USB, or programming is in progress.", "style": "info" }));
            return;
        }
        self.forget_tag();
        let bases = tagprobe::list();
        if bases.is_empty() {
            self.raise(json!({ "kind": "alert", "title": "Choose a Base",
                "text": "No ST-LINK is on USB, or programming is in progress.", "style": "info" }));
            return;
        }
        self.raise_base_prompt(&bases);
    }

    fn choose_base(&mut self, index: usize) {
        let bases = tagprobe::list();
        if let Some(b) = bases.get(index) {
            self.use_base(b.clone());
        }
    }

    /// qtprogram's `chooseBase`, with the base's identity added.
    fn use_base(&mut self, base: UsbDev) {
        if base.serial.is_empty() && tagprobe::list().len() > 1 {
            self.have_base = false;
            self.set(
                Check::Base,
                Level::Fail,
                "Cannot read this base's ST-LINK serial, so it cannot be told from the others; unplug the other bases.",
            );
            return;
        }
        if !base.serial.is_empty() {
            self.save_setting(key::BASE_SERIAL, base.serial.clone());
        }
        self.base_identity = tagprobe::identify(&base);
        let serial = if base.serial.is_empty() {
            "(serial not readable; the only base)".to_owned()
        } else {
            base.serial.clone()
        };
        let text = match &self.base_identity {
            Some(id) => format!(
                "ST-LINK {serial}: {} {}",
                identity_field(id, "base").unwrap_or("base"),
                identity_field(id, "fw").unwrap_or("")
            ),
            None => format!("ST-LINK {serial} (older base firmware: field-safe driver)"),
        };
        self.base = Some(base);
        self.have_base = true;
        self.set(Check::Base, Level::Ok, text);
        self.base_ready();
    }

    fn base_ready(&mut self) {
        if self.auto_attach() && self.level(Check::Programmer) == Level::Ok {
            self.attach();
        } else {
            self.set(
                Check::Swd,
                Level::Pending,
                "Press Attach to connect (this resets the tag).",
            );
        }
    }

    pub(crate) fn driver(&self) -> Driver {
        Driver::for_identity(self.base_identity.as_deref())
    }

    // ---- attach ------------------------------------------------------

    pub(crate) fn attach(&mut self) {
        if self.busy() {
            return;
        }
        self.forget_tag();
        if !self.have_base {
            self.check_base();
        }
        let Some(base) = self.find_base() else {
            self.set(
                Check::Base,
                Level::Warn,
                "The base is not on USB. Plug it in.",
            );
            if self.base_poll.is_none() {
                self.base_poll = Some(Instant::now() + BASE_POLL);
            }
            return;
        };
        self.base = Some(base.clone());
        for c in [Check::Supply, Check::Tag, Check::TagState] {
            self.set(c, Level::Pending, "");
        }
        self.set(Check::Swd, Level::Pending, "Connecting over SWD");
        self.worker_busy = true;
        self.spawn_work(
            move || tagprobe::connect_check(&base),
            |s, result| {
                s.worker_busy = false;
                match result {
                    Err(e) => {
                        s.connect = None;
                        s.set(
                            Check::Swd,
                            Level::Fail,
                            format!("{e}. Is a tag in the base, and powered?"),
                        );
                        s.check_supply();
                        s.check_firmware();
                    }
                    Ok(c) => {
                        s.set(
                            Check::Swd,
                            Level::Ok,
                            format!("0x{:03x} {}", c.device_id, c.device_name),
                        );
                        s.connect = Some(c);
                        s.check_supply();
                        s.attach_tag(ATTACH_TRIES);
                    }
                }
            },
        );
    }

    /// The chosen base, re-found on USB by serial (or the only base).
    pub(crate) fn find_base(&self) -> Option<UsbDev> {
        let bases = tagprobe::list();
        let saved = self.settings.string(key::BASE_SERIAL);
        bases
            .iter()
            .find(|b| !saved.is_empty() && b.serial == saved)
            .cloned()
            .or_else(|| (bases.len() == 1).then(|| bases[0].clone()))
    }

    fn attach_tag(&mut self, tries: u32) {
        let base = self.base.clone().unwrap_or_default();
        if self.tag.attach(&base)
            && let Ok(info) = self.tag.tag_info()
        {
            self.set(
                Check::Tag,
                Level::Ok,
                format!("{}, {}", info.board_desc, info.githash),
            );
            self.info = Some(info);
            self.read_status();
            self.check_tag_state();
            self.detect_target();
            self.status_poll = Some(Instant::now() + STATUS_POLL);
            self.check_firmware();
            return;
        }
        if tries > 1 && !self.attached() {
            self.attach_retry = Some((Instant::now() + ATTACH_RETRY, tries - 1));
            return;
        }
        self.tag.detach();
        self.info = None;
        self.set(
            Check::Tag,
            Level::Warn,
            "No firmware answers: a blank board, or one in its bootloader. Choose the tag type.",
        );
        self.set(Check::TagState, Level::Pending, "");
        self.check_firmware();
    }

    /// Re-attaches to the base after programming; true if the tag answers.
    pub(crate) fn reattach(&mut self) -> bool {
        self.tag.detach();
        match self.find_base() {
            Some(b) => self.tag.attach(&b),
            None => false,
        }
    }

    pub(crate) fn stop_status_poll(&mut self) {
        self.status_poll = None;
    }

    pub(crate) fn resume_status_poll(&mut self) {
        self.status_poll = Some(Instant::now() + STATUS_POLL);
    }

    pub(crate) fn forget_tag(&mut self) {
        self.status_poll = None;
        self.attach_retry = None;
        self.tag.detach();
        self.info = None;
        self.status = None;
        self.offset = None;
        self.missed = 0;
    }

    fn check_supply(&mut self) {
        let volts = self.connect.as_ref().and_then(|c| c.voltage);
        let device = self.connect.as_ref().map(|c| c.device_id);
        match volts {
            None => self.set(Check::Supply, Level::Pending, ""),
            Some(v) if device == Some(DEV_L432) && v > L432_MAX_VOLTS => self.set(
                Check::Supply,
                Level::Fail,
                format!("{v:.2} V: above the STM32L432's 3.6 V maximum. Lower the supply."),
            ),
            Some(v) if v < LOW_VOLTS => self.set(
                Check::Supply,
                Level::Warn,
                format!("{v:.2} V: low; programming may fail."),
            ),
            Some(v) => self.set(Check::Supply, Level::Ok, format!("{v:.2} V")),
        }
    }

    pub(crate) fn read_status(&mut self) -> bool {
        match self.tag.status() {
            Ok(status) => {
                self.offset = Some((status.millis - now_ms()) as f64 / 1000.0);
                self.status = Some(status);
                self.missed = 0;
                true
            }
            Err(_) => false,
        }
    }

    pub(crate) fn check_tag_state(&mut self) {
        let Some(status) = &self.status else { return };
        let name = state_name(status.state);
        let state = TagState::try_from(status.state).unwrap_or(TagState::StateUnspecified);
        match state {
            TagState::Idle => self.set(Check::TagState, Level::Ok, name),
            TagState::Aborted | TagState::Finished => self.set(
                Check::TagState,
                Level::Warn,
                format!("{name}: holds a log, which programming erases. Download it with Tag Monitor first."),
            ),
            TagState::Test | TagState::SReset => {
                self.set(Check::TagState, Level::Pending, format!("{name} in progress"))
            }
            _ => self.set(
                Check::TagState,
                Level::Warn,
                format!("{name}: deployed. Programming stops it and erases its log."),
            ),
        }
    }

    fn poll(&mut self) {
        if self.session_running() || !self.attached() || self.worker_busy {
            return;
        }
        if self.read_status() {
            self.check_tag_state();
            return;
        }
        self.missed += 1;
        if self.missed >= MISSED_POLLS {
            self.forget_tag();
            self.set(
                Check::Tag,
                Level::Warn,
                "The tag stopped answering. Press Attach.",
            );
            self.set(Check::TagState, Level::Pending, "");
            self.set(
                Check::Swd,
                Level::Pending,
                "Press Attach to connect (this resets the tag).",
            );
        }
    }

    fn next_tag(&mut self) {
        if self.busy() {
            return;
        }
        self.forget_tag();
        self.connect = None;
        self.session = None;
        self.result = (String::new(), None);
        self.set(
            Check::Swd,
            Level::Pending,
            "Swap the tag, then press Attach.",
        );
        for c in [Check::Supply, Check::Tag, Check::TagState] {
            self.set(c, Level::Pending, "");
        }
        self.check_firmware();
    }

    fn stop_tag(&mut self) {
        if self.attached() && self.tag.stop().is_ok() {
            self.read_status();
            self.check_tag_state();
        }
    }

    fn erase_tag(&mut self) {
        let erasable = self.status.as_ref().is_some_and(|s| {
            matches!(
                TagState::try_from(s.state),
                Ok(TagState::Finished | TagState::Aborted)
            )
        });
        if self.attached() && erasable && self.tag.erase().is_ok() {
            self.read_status();
            self.check_tag_state();
        }
    }

    fn sync_clock(&mut self) {
        if !self.attached() {
            return;
        }
        if self.tag.set_rtc().is_err() {
            self.alert("Sync Clock", "The tag did not accept the clock setting.");
        }
        self.read_status();
    }

    // ---- firmware ----------------------------------------------------

    fn refresh_catalog(&mut self) {
        if self.refreshing {
            return;
        }
        self.refreshing = true;
        self.refresh_error = None;
        self.check_firmware();
        let catalog = self.catalog.clone();
        self.spawn_work(
            move || catalog.refresh(),
            |s, result| {
                s.refreshing = false;
                if let Err(e) = result {
                    warn!("Could not list firmware releases: {e}");
                    s.refresh_error = Some(e);
                }
                s.catalog.load();
                let release = s.chosen_release();
                if release.is_some() && s.image.is_none() && s.fetching.is_none() {
                    s.load_image();
                } else {
                    s.check_firmware();
                }
            },
        );
    }

    fn choose_release(&mut self, tag: &str) {
        self.save_setting(key::RELEASE, tag);
        self.remove_setting(key::FILE);
        self.remove_setting(key::LEGACY_IMAGE);
        self.load_image();
    }

    fn choose_file(&mut self, path: &str) {
        if path.is_empty() {
            return;
        }
        self.save_setting(key::FILE, path);
        self.remove_setting(key::RELEASE);
        if let Err(e) = self.settings.add_recent_file(path) {
            warn!("{e}");
        }
        self.load_image();
    }

    fn choose_target(&mut self, name: &str) {
        self.target = name.to_owned();
        self.target_detected = false;
        self.save_setting(key::TARGET, name);
        self.load_image();
    }

    pub(crate) fn chosen_release(&self) -> Option<String> {
        Some(self.settings.string(key::RELEASE)).filter(|s| !s.is_empty())
    }

    pub(crate) fn chosen_file(&self) -> Option<String> {
        Some(self.settings.firmware_file())
            .filter(|s| !s.is_empty())
            .filter(|_| self.chosen_release().is_none())
    }

    fn load_image(&mut self) {
        self.image = None;
        if let Some(file) = self.chosen_file() {
            let image = load_firmware_image(Path::new(&file), None);
            if image.identity.found {
                self.target = image.identity.target.clone();
            }
            self.image = Some(image);
        } else if let Some(release) = self.chosen_release() {
            if !self.catalog.is_cached(&release) {
                let has_archive = self
                    .catalog
                    .release(&release)
                    .is_some_and(|r| !r.archive_url.is_empty());
                if has_archive {
                    self.fetch(&release);
                }
            } else if self.catalog.targets(&release).contains(&self.target) {
                let path = self.catalog.image_path(&release, &self.target);
                self.image = Some(load_firmware_image(&path, Some(&release)));
            }
        }
        self.check_firmware();
    }

    fn fetch(&mut self, release: &str) {
        if self.fetching.is_some() {
            return;
        }
        info!("Fetching {release}");
        self.fetching = Some(release.to_owned());
        self.fetch_errors.remove(release);
        let catalog = self.catalog.clone();
        let tag = release.to_owned();
        self.spawn_work(move || catalog.fetch(&tag), {
            let tag = release.to_owned();
            move |s, result| {
                s.fetching = None;
                if let Err(e) = result {
                    warn!("Fetching {tag} failed: {e}");
                    s.fetch_errors.insert(tag.clone(), e);
                }
                s.catalog.load();
                if s.chosen_release().as_deref() == Some(tag.as_str()) {
                    s.load_image();
                }
            }
        });
    }

    fn detect_target(&mut self) {
        let Some(info) = &self.info else { return };
        let Some(release) = self.chosen_release() else {
            return;
        };
        let detected = info
            .source_path
            .rsplit('/')
            .find(|p| !p.is_empty())
            .unwrap_or("")
            .to_owned();
        if !detected.is_empty() && self.catalog.targets(&release).contains(&detected) {
            let changed = detected != self.target;
            self.target = detected.clone();
            self.target_detected = true;
            self.save_setting(key::TARGET, detected.clone());
            if changed || self.image.is_none() {
                self.load_image();
            }
        }
    }

    /// qtprogram's `checkFirmware`, in its order.
    pub(crate) fn check_firmware(&mut self) {
        let release = self.chosen_release();
        let file = self.chosen_file();
        if release.is_none() && file.is_none() {
            self.set(Check::Firmware, Level::Fail, "Choose the firmware.");
            return;
        }
        if let Some(rel) = &release {
            if !self.catalog.is_cached(rel) {
                let (level, text) = if let Some(e) = self.fetch_errors.get(rel) {
                    (Level::Fail, format!("{rel} could not be downloaded: {e}"))
                } else if self.fetching.as_deref() == Some(rel.as_str()) {
                    (Level::Pending, format!("Downloading {rel}"))
                } else if self.refreshing {
                    (Level::Pending, format!("Looking up {rel} on GitHub"))
                } else if let Some(e) = &self.refresh_error {
                    (
                        Level::Fail,
                        format!("{rel} is not downloaded, and GitHub cannot be reached: {e}"),
                    )
                } else {
                    (Level::Fail, format!("{rel} is not a published release."))
                };
                self.set(Check::Firmware, level, text);
                return;
            }
            if !self.catalog.targets(rel).contains(&self.target) {
                let text = if self.target.is_empty() {
                    "Choose the tag type.".to_owned()
                } else {
                    format!("{rel} has no image for {}.", self.target)
                };
                self.set(Check::Firmware, Level::Fail, text);
                return;
            }
        }
        let Some(image) = self.image.clone() else {
            self.set(Check::Firmware, Level::Fail, "Choose the firmware.");
            return;
        };
        if !image.error.is_empty() {
            self.set(Check::Firmware, Level::Fail, image.error.clone());
            return;
        }
        if let Some(c) = &self.connect {
            let page = image.plan.page_size;
            let mismatch = (page == 4096 && c.device_id != DEV_U375)
                || (page == 2048 && c.device_id != DEV_L432);
            if mismatch {
                self.set(
                    Check::Firmware,
                    Level::Fail,
                    format!(
                        "This image is for an {}, and the tag is an {}.",
                        mcu_for_page(page),
                        mcu_for_device(c.device_id)
                    ),
                );
                return;
            }
        }
        if let Some(runs) = self.tag_target()
            && image.identity.found
            && !image.identity.target.is_empty()
            && runs != image.identity.target
        {
            self.set(
                Check::Firmware,
                Level::Warn,
                format!(
                    "The image is for {}, and the tag runs {runs}.",
                    image.identity.target
                ),
            );
            return;
        }
        if !image.warning.is_empty() {
            self.set(Check::Firmware, Level::Warn, image.warning.clone());
            return;
        }
        if let Some(q) = self.qualification_warning() {
            self.set(Check::Firmware, Level::Warn, q);
            return;
        }
        let how = if image.manifest_verified {
            "verified against its manifest"
        } else {
            "no manifest to check"
        };
        self.set(
            Check::Firmware,
            Level::Ok,
            format!("{}, {how}", image.display_name()),
        );
    }

    /// The target the attached tag runs, from its source path.
    pub(crate) fn tag_target(&self) -> Option<String> {
        let info = self.info.as_ref()?;
        let t = info.source_path.rsplit('/').find(|p| !p.is_empty())?;
        Some(t.to_owned())
    }

    pub(crate) fn qualification_warning(&self) -> Option<String> {
        let image = self.image.as_ref()?;
        let Some(release) = &image.release else {
            return Some(format!(
                "{} is a local file, not a qualified release.",
                image.display_name()
            ));
        };
        let target = if image.identity.target.is_empty() {
            &self.target
        } else {
            &image.identity.target
        };
        match self.catalog.release(release) {
            Some(r) if r.listed && !r.is_qualified(target) => Some(format!(
                "{release} is not qualified for {target}. See the Release Notes."
            )),
            Some(r) if r.listed => None,
            _ => Some(format!(
                "Cannot tell whether {release} is qualified for {target}: GitHub has not been reached."
            )),
        }
    }

    // ---- record ------------------------------------------------------

    pub(crate) fn default_record_path(&self) -> PathBuf {
        self.cfg
            .documents_dir
            .join("Tag Programmer")
            .join("programmed-tags.csv")
    }

    fn raise_record_prompt(&mut self) {
        let path = self.default_record_path();
        self.raise(json!({
            "kind": "recordFile",
            "defaultPath": path.to_string_lossy(),
            "exists": path.exists(),
        }));
    }

    fn open_record(&mut self, path: &Path) {
        let mut record = RecordLog::new();
        match record.open(path) {
            Ok(()) => {
                self.save_setting(key::RECORD_PATH, path.to_string_lossy().into_owned());
                self.record = Some(record);
            }
            Err(e) => self.alert("Record File", &e),
        }
        self.check_record();
    }

    fn create_record(&mut self, path: &Path) {
        let mut record = RecordLog::new();
        match record.create(path) {
            Ok(()) => {
                self.save_setting(key::RECORD_PATH, path.to_string_lossy().into_owned());
                self.record = Some(record);
            }
            Err(e) => self.alert("Record File", &e),
        }
        self.check_record();
    }

    pub(crate) fn check_record(&mut self) {
        match &self.record {
            None => self.set(
                Check::Record,
                Level::Fail,
                "No record file. Create or choose one.",
            ),
            Some(r) => match r.writable() {
                Err(e) => self.set(Check::Record, Level::Fail, e),
                Ok(()) => {
                    let name = r
                        .path()
                        .and_then(Path::file_name)
                        .map(|n| n.to_string_lossy().into_owned())
                        .unwrap_or_default();
                    let n = r.count();
                    let rows = if n == 1 {
                        "1 row".to_owned()
                    } else {
                        format!("{n} rows")
                    };
                    self.set(Check::Record, Level::Ok, format!("{name} ({rows})"));
                }
            },
        }
    }

    // ---- programming gate --------------------------------------------

    /// qtprogram's `canProgram`: the first reason it may not program.
    pub(crate) fn can_program(&self) -> Result<(), String> {
        if self.session_running() {
            return Err("Programming is in progress.".into());
        }
        if self.attach_retry.is_some() || self.worker_busy {
            return Err("Connecting to the tag.".into());
        }
        for c in [Check::Programmer, Check::Base, Check::Swd, Check::Record] {
            if self.level(c) != Level::Ok {
                return Err(self.text(c).to_owned());
            }
        }
        if self.level(Check::Supply) == Level::Fail {
            return Err(self.text(Check::Supply).to_owned());
        }
        let fw = self.level(Check::Firmware);
        if fw == Level::Fail
            || fw == Level::Pending
            || !self.image.as_ref().is_some_and(|i| i.usable())
        {
            return Err(self.text(Check::Firmware).to_owned());
        }
        Ok(())
    }

    pub(crate) fn calibrated_tag(&self) -> bool {
        use tagcore::proto::TagType;
        let t = self.info.as_ref().map(|i| i.tag_type).unwrap_or_else(|| {
            self.image
                .as_ref()
                .map(|i| i.identity.tag_type)
                .unwrap_or(0)
        });
        t == TagType::Compasstag as i32 || t == TagType::Imutag as i32
    }

    fn program_preview(&self) -> Value {
        if let Err(reason) = self.can_program() {
            return json!({ "ok": false, "reason": reason });
        }
        let image = self.image.as_ref().expect("usable image");
        let supply = self.text(Check::Supply);
        let tag = match (&self.info, &self.status) {
            (Some(info), status) => format!(
                "{}  {}  UUID {}  {supply}",
                self.tag_target().unwrap_or_default(),
                status
                    .as_ref()
                    .map(|s| state_name(s.state))
                    .unwrap_or_default(),
                info.uuid
            ),
            (None, _) => format!("No firmware answers (blank board); {supply}"),
        };
        let commit = if image.identity.git_hash.is_empty() {
            image.manifest_commit.chars().take(8).collect()
        } else {
            image.identity.git_hash.clone()
        };
        let mut lost = Vec::new();
        let mut kept = Vec::new();
        if self.info.is_some() {
            lost.push("The tag's log. Download it first with Tag Monitor.".to_owned());
        }
        lost.push("The tag's stored configuration.".to_owned());
        if self.calibrated_tag() {
            if image.plan.no_layout {
                lost.push("Its calibration constants: this image predates the flash layout record, so all of flash is erased.".to_owned());
            } else if !image.plan.keeps_calibration() {
                lost.push(
                    "Its calibration constants: this image keeps no calibration page.".to_owned(),
                );
            } else {
                kept.push("Its calibration constants.".to_owned());
            }
        }
        kept.push("The RTC trim and the board UUID.".to_owned());
        let mut notes = Vec::new();
        if let Some(runs) = self.tag_target()
            && image.identity.found
            && !image.identity.target.is_empty()
            && runs != image.identity.target
        {
            notes.push(format!(
                "The image is for {}, and the tag runs {runs}. Is it the right image?",
                image.identity.target
            ));
        }
        if let Some(q) = self.qualification_warning() {
            notes.push(q);
        }
        json!({
            "ok": true,
            "reason": "",
            "tag": tag,
            "image": format!("{}, commit {commit}\nSHA-256 {}", image.display_name(), image.sha256),
            "erase": image.plan.summary(),
            "lost": lost,
            "kept": kept,
            "notes": notes,
        })
    }

    fn label_notes(&self, label: &str) -> Vec<String> {
        let label = label.trim();
        let mut notes = Vec::new();
        let (Some(record), Some(uuid)) = (&self.record, self.info.as_ref().map(|i| i.uuid.clone()))
        else {
            return notes;
        };
        if !label.is_empty() {
            let uuids = record.uuids_for_label(label);
            if !uuids.is_empty() && !(uuids.len() == 1 && uuids[0] == uuid) {
                notes.push(format!(
                    "{label} is already recorded for UUID {}.",
                    uuids.join(", ")
                ));
            }
        }
        let labels = record.labels_for_uuid(&uuid);
        if !labels.is_empty() && !labels.iter().any(|l| l == label) {
            notes.push(format!("This tag is recorded as {}.", labels.join(", ")));
        }
        notes
    }

    fn retest_default_label(&self) -> String {
        match (&self.record, &self.info) {
            (Some(r), Some(info)) => r
                .labels_for_uuid(&info.uuid)
                .last()
                .cloned()
                .unwrap_or_default(),
            _ => String::new(),
        }
    }

    /// The erase that goes with the chosen image, for tagprobe.
    pub(crate) fn probe_erase(&self, image: &FirmwareImage) -> tagprobe::Erase {
        match image.plan.erase {
            Erase::All => tagprobe::Erase::All,
            Erase::BelowCalibration => tagprobe::Erase::Range {
                start: 0x0800_0000,
                end: 0x0800_0000
                    + (image.plan.last_erased_page as u64 + 1) * u64::from(image.plan.page_size),
            },
            _ => tagprobe::Erase::ImagePages,
        }
    }

    /// The probe-rs target for the attached part and the image.
    pub(crate) fn chip(&self, image: &FirmwareImage) -> Result<&'static str, String> {
        let device =
            self.connect
                .as_ref()
                .map(|c| c.device_id)
                .unwrap_or(if image.plan.page_size == 4096 {
                    DEV_U375
                } else {
                    DEV_L432
                });
        tagprobe::chip_for(device, None)
    }

    /// Sets a setting, logging a failure to save it.
    pub(crate) fn save_setting(&mut self, name: &str, value: impl Into<Value>) {
        if let Err(e) = self.settings.set(name, value) {
            warn!("{e}");
        }
    }

    fn remove_setting(&mut self, name: &str) {
        if let Err(e) = self.settings.remove(name) {
            warn!("{e}");
        }
    }
}
