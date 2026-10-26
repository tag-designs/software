//! The station: qtprogram's `Station` (`station.cpp`), ported.
//!
//! Eight checks gate programming; the base and the tag are found, attached
//! and polled; the firmware, tag type and record are chosen; prompts ask the
//! UI for decisions. Texts and rules are qtprogram's except where the
//! contract says otherwise (probe-rs instead of STM32_Programmer_CLI).
//!
//! # State and ownership
//!
//! One [`Station`] holds all of the engine's state and is owned by the
//! engine thread (`super::run`). Its methods run only there. Work that would
//! block that thread for seconds -- listing and downloading releases, the
//! SWD connect check, programming and the flash read-back -- runs through
//! [`Station::spawn_work`], whose result comes back as a `Msg::Done`
//! closure, so no state is ever touched off the engine thread. A worker
//! that owns the base sets [`Station::worker_busy`] so that nothing else
//! opens the ST-LINK meanwhile.
//!
//! Monitor traffic to the tag (`tagcore::Tag`: attach, status, stop, erase,
//! SetRtc, self-tests) is synchronous and runs on the engine thread, as on
//! qtprogram's GUI thread.
//!
//! # Attaching resets the tag
//!
//! Both the SWD connect check (`tagprobe::connect_check`) and the monitor
//! attach go through reset, so every [`Station::attach`] ends whatever the
//! tag was doing. The engine never attaches on its own unless the user
//! turned on automatic attach; with it off, the SWD row asks the user to
//! press Attach and says that it resets the tag.
//!
//! # Siblings
//!
//! The programming session (`session.rs`) adds the program/retest step
//! machine to this type in a second `impl Station`; the view (`view.rs`)
//! adds `view_state`, which reads these fields to build the contract's JSON.
//! See `design/engine.md`.

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

/// The STM32L432's absolute maximum VDD, in volts. A higher supply on an
/// L432 fails the Supply check and so blocks programming (qtprogram's
/// `kL432MaxVolts`). The U375 boards run from 3.7 V and are not limited.
const L432_MAX_VOLTS: f32 = 3.6;
/// The supply, in volts, below which the Supply check warns that
/// programming may fail. A warning only: it does not block programming.
const LOW_VOLTS: f32 = 1.8;
/// Consecutive failed status polls after which the tag is forgotten and
/// the user is asked to attach again.
const MISSED_POLLS: u32 = 5;
/// Monitor attach attempts after a successful SWD connect check. The
/// ST-LINK can still be held for a moment after probe-rs lets it go (on
/// macOS the first open fails), so an attach that reached nothing is retried.
const ATTACH_TRIES: u32 = 10;
/// The wait between monitor attach attempts.
const ATTACH_RETRY: Duration = Duration::from_millis(200);
/// The tag clock offset, in seconds, beyond which the view shows the clock
/// in amber with "(press Sync)".
pub(crate) const CLOCK_TOLERANCE_S: f64 = 2.0;
/// How often USB is listed for a base while none is found.
const BASE_POLL: Duration = Duration::from_millis(1000);
/// How often an attached tag's status is read while no session runs.
const STATUS_POLL: Duration = Duration::from_millis(400);

/// The station checks, in display order.
///
/// The discriminant indexes [`Station::checks`] and [`CHECK_NAMES`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Check {
    /// The programming path: always OK, reporting the probe-rs version (there
    /// is no external programmer to find, unlike qtprogram's
    /// STM32_Programmer_CLI).
    Programmer = 0,
    /// The USB base (ST-LINK): found, chosen and identified (`F6 99`).
    Base,
    /// The SWD connect check: the part's device id and name, or why it
    /// failed.
    Swd,
    /// The target voltage the ST-LINK measured, against the part's limits.
    Supply,
    /// The tag's firmware: its board and commit when it answers the monitor,
    /// else a warning that it is blank or in its bootloader.
    Tag,
    /// The tag's monitor state, and whether programming would lose a log or
    /// stop a deployment.
    TagState,
    /// The chosen image: downloaded, for this tag type and part, and
    /// qualified.
    Firmware,
    /// The record CSV: open and writable, with its row count.
    Record,
}

/// The checks' names as the view shows them, indexed by [`Check`].
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

/// A check's level: the light the view shows beside it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Level {
    /// Not decided yet, or waiting on something (grey).
    Pending,
    /// Passed (green).
    Ok,
    /// Passed with a caveat the user should read (amber). A warning never
    /// blocks programming; [`Station::can_program`] requires Programmer,
    /// Base, SWD and Record to be OK and Supply and Firmware not to fail.
    Warn,
    /// Failed (red).
    Fail,
}

impl Level {
    /// The level's name in the view state: `pending`, `ok`, `warn` or `fail`.
    pub(crate) fn name(self) -> &'static str {
        match self {
            Level::Pending => "pending",
            Level::Ok => "ok",
            Level::Warn => "warn",
            Level::Fail => "fail",
        }
    }
}

/// One check's state: a level and the final text the view shows.
#[derive(Debug, Clone)]
pub(crate) struct CheckStatus {
    /// The light.
    pub level: Level,
    /// The text beside it, final; empty while pending with nothing to say.
    pub text: String,
}

/// A question for the UI (`state.prompt`).
///
/// At most one is outstanding. Raising another replaces it; the UI shows a
/// dialog once per `id` and answers with the command the contract names for
/// its `kind`, which clears it.
#[derive(Debug, Clone)]
pub(crate) struct Prompt {
    /// A fresh number per prompt, so the UI can tell a new question from the
    /// one it is already showing.
    pub id: u64,
    /// The prompt's JSON without its `id`: `kind` and the kind's fields.
    pub body: Value,
}

/// Station state, owned by the engine thread.
///
/// qtprogram's `Station` plus the parts of its `MainWindow` that held state
/// (the batch counts, the last result, the outstanding dialog). The session
/// (`session.rs`) and the view (`view.rs`) are further `impl` blocks on this
/// type. See the module documentation for the threading rules.
pub(crate) struct Station {
    /// Where files live, from [`super::spawn`].
    pub(crate) cfg: EngineConfig,
    /// The engine thread's own channel, for [`Self::spawn_work`] results and
    /// for the deferred record open in [`Self::begin_checks`].
    tx: Sender<Msg>,
    /// The settings file, written through at every change.
    pub(crate) settings: Settings,
    /// The release catalog. Shared with the workers that refresh it and
    /// download into it; the engine thread re-`load`s it when they finish.
    pub(crate) catalog: Arc<Catalog>,
    /// A catalog refresh (the GitHub listing) is running on a worker.
    pub(crate) refreshing: bool,
    /// Why the last refresh failed, shown by the Firmware check when the
    /// chosen release is not cached.
    pub(crate) refresh_error: Option<String>,
    /// The release being downloaded on a worker, if any; one at a time.
    pub(crate) fetching: Option<String>,
    /// Why each failed download failed, by release tag, until it is retried.
    pub(crate) fetch_errors: HashMap<String, String>,

    /// The eight checks, indexed by [`Check`].
    pub(crate) checks: Vec<CheckStatus>,
    /// The chosen base as last found on USB. Its bus address can change on a
    /// replug; [`Self::find_base`] re-finds it by serial.
    pub(crate) base: Option<UsbDev>,
    /// The base's `F6 99` identity string (`key=value;...`), or `None` for an
    /// older base firmware that does not answer it; picks the probe
    /// [`Driver`].
    pub(crate) base_identity: Option<String>,
    /// A base has been chosen and is usable.
    pub(crate) have_base: bool,
    /// The last successful SWD connect check: part, device id and target
    /// voltage. Cleared by Next Tag and by a failed check.
    pub(crate) connect: Option<Connect>,
    /// The monitor link to the tag, through the base.
    pub(crate) tag: Tag,
    /// The attached tag's `TagInfo` (UUID, commit, board), while its
    /// firmware answers.
    pub(crate) info: Option<TagInfo>,
    /// The last status read from the tag.
    pub(crate) status: Option<Status>,
    /// The tag clock minus the host clock at the last status read, in
    /// seconds.
    pub(crate) offset: Option<f64>,
    /// Consecutive failed status polls (see [`MISSED_POLLS`]).
    missed: u32,
    /// The next monitor attach attempt and the tries left, while attaching.
    attach_retry: Option<(Instant, u32)>,
    /// The next USB listing, while no base is found.
    base_poll: Option<Instant>,
    /// The next status poll, while a tag is attached and no session runs.
    status_poll: Option<Instant>,
    /// A worker owns the base (connect check, programming, read-back). While it
    /// is set nothing else opens the ST-LINK: polls, attaches and Choose Base
    /// wait.
    pub(crate) worker_busy: bool,

    /// The loaded image for the chosen release and tag type, or file.
    pub(crate) image: Option<FirmwareImage>,
    /// The tag type (a release target name such as `BitTag`); empty for
    /// none.
    pub(crate) target: String,
    /// [`Self::target`] came from the attached tag rather than the user.
    pub(crate) target_detected: bool,
    /// The open record CSV.
    pub(crate) record: Option<RecordLog>,
    /// The last label programmed or retested, shown when the attached tag
    /// has none recorded and offered by the Program dialog.
    pub(crate) last_label: String,
    /// Rows recorded OK since the app started (the batch line).
    pub(crate) ok_count: u32,
    /// Rows recorded not OK since the app started.
    pub(crate) failed_count: u32,
    /// The last session's result line and whether it was OK (`None`: no
    /// result to show). Cleared by Next Tag and by starting a session.
    pub(crate) result: (String, Option<bool>),

    /// The current or last session. It stays after the session ends, so its
    /// message stays on the step line until Next Tag.
    pub(crate) session: Option<SessionState>,
    /// The outstanding question for the UI.
    pub(crate) prompt: Option<Prompt>,
    /// The last prompt id issued.
    prompt_seq: u64,
    /// `start` has been received; later ones are ignored.
    started: bool,
}

/// Milliseconds since the Unix epoch, now; 0 if the clock is before it.
pub(crate) fn now_ms() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

/// The protobuf name of a tag state, as `TagState_Name` gives it, or the
/// number for a value this build does not know.
pub(crate) fn state_name(state: i32) -> String {
    TagState::try_from(state)
        .map(|s| s.as_str_name().to_owned())
        .unwrap_or_else(|_| state.to_string())
}

/// The protobuf name of a test result, as `TestResult_Name` gives it, or
/// the number for a value this build does not know.
pub(crate) fn test_name(result: i32) -> String {
    TestResult::try_from(result)
        .map(|s| s.as_str_name().to_owned())
        .unwrap_or_else(|_| result.to_string())
}

/// The family an image's flash page size implies: 4 KB pages are the
/// STM32U375, anything else the STM32L432.
pub(crate) fn mcu_for_page(page_size: u32) -> &'static str {
    if page_size == 4096 {
        "STM32U375"
    } else {
        "STM32L432"
    }
}

/// The family name for an SWD device id, for the Firmware check's
/// mismatch text.
fn mcu_for_device(device_id: u16) -> &'static str {
    match device_id {
        DEV_U375 => "STM32U375",
        DEV_L432 => "STM32L432",
        _ => "unknown part",
    }
}

/// The value of one `key=` field of a base identity string
/// (`base=...;fw=...`), or `None` when the key is absent.
pub(crate) fn identity_field<'a>(identity: &'a str, key: &str) -> Option<&'a str> {
    identity
        .split(';')
        .find_map(|kv| kv.strip_prefix(key)?.strip_prefix('='))
}

impl Station {
    /// Builds the station from the settings file and the cached catalog.
    ///
    /// Runs on the engine thread before any message. Loads the settings (absent
    /// file: defaults) and restores the last label and tag type; every check
    /// starts pending. Touches no USB device: that waits for `start`.
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

    /// Sets one check's level and text.
    pub(crate) fn set(&mut self, check: Check, level: Level, text: impl Into<String>) {
        self.checks[check as usize] = CheckStatus {
            level,
            text: text.into(),
        };
    }

    /// One check's level.
    pub(crate) fn level(&self, check: Check) -> Level {
        self.checks[check as usize].level
    }

    /// One check's text.
    pub(crate) fn text(&self, check: Check) -> &str {
        &self.checks[check as usize].text
    }

    /// Runs `work` on a worker thread and applies its result on the engine
    /// thread with `apply`.
    ///
    /// The result is posted back as a `Msg::Done`, so `apply` sees the station
    /// as it is when the message is handled, not as it was when the work began:
    /// it must re-check anything that may have changed meanwhile. The caller sets
    /// any busy flag (such as [`Self::worker_busy`]) and `apply` clears it.
    ///
    /// If the engine thread has gone, the result is dropped. A panic in `work`
    /// kills only the worker, and `apply` never runs -- so a busy flag it was to
    /// clear stays set.
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

    /// A sender for progress messages from a worker, which posts them as
    /// `Msg::Done` closures (as `run_programmer` does with probe-rs progress).
    pub(crate) fn sender(&self) -> Sender<Msg> {
        self.tx.clone()
    }

    /// Raises a prompt, replacing any outstanding one, with a fresh id.
    pub(crate) fn raise(&mut self, body: Value) {
        self.prompt_seq += 1;
        self.prompt = Some(Prompt {
            id: self.prompt_seq,
            body,
        });
    }

    /// Raises a warning-style `alert` prompt: a message box with OK.
    pub(crate) fn alert(&mut self, title: &str, text: &str) {
        self.raise(json!({ "kind": "alert", "title": title, "text": text, "style": "warning" }));
    }

    /// The nearest armed timer, for the engine loop's wait; `None` when no
    /// timer is armed.
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

    /// Fires every timer due at `now`, re-arming the periodic ones first.
    ///
    /// The base poll and status poll re-arm themselves before running, so a
    /// poll that disarms its own timer (by forgetting the tag, or finding the
    /// base) wins. The attach retry is one-shot. The session tick re-arms for
    /// 1 s later, then runs a step, which may stop or restart it.
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

    /// A session is running (not merely kept for its final message).
    pub(crate) fn session_running(&self) -> bool {
        self.session.as_ref().is_some_and(|s| s.running)
    }

    /// The monitor link to a tag is open.
    pub(crate) fn attached(&self) -> bool {
        self.tag.is_attached()
    }

    /// Something owns the base or the tag: a session, a worker, or an
    /// attach in progress. Base checks, attaches and Next Tag wait for it.
    fn busy(&self) -> bool {
        self.session_running() || self.worker_busy || self.attach_retry.is_some()
    }

    // ---- commands and queries ----------------------------------------

    /// Applies one UI command (the contract's command table).
    ///
    /// Runs on the engine thread. Missing or mistyped arguments read as empty
    /// strings, `false` or index 0; an unknown command is logged as a warning.
    /// The answers to prompts (`answer_auto_attach`, `choose_base`,
    /// `record_*`, `prompt_cancel`) clear the outstanding prompt first.
    /// `save_text` writes the file synchronously and raises an alert if it
    /// cannot.
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

    /// Answers one UI query (the contract's query table).
    ///
    /// Runs on the engine thread and changes no state. An unknown name answers
    /// `{ "error": "unknown query" }`.
    pub(crate) fn query(&mut self, name: &str, args: &Value) -> Value {
        match name {
            "program_preview" => self.program_preview(),
            "label_notes" => {
                json!(self.label_notes(args.get("label").and_then(Value::as_str).unwrap_or("")))
            }
            "label_conflict" => {
                let uuid = self.info.as_ref().map(|i| i.uuid.as_str()).unwrap_or("");
                json!(self.label_conflict(
                    args.get("label").and_then(Value::as_str).unwrap_or(""),
                    uuid
                ))
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

    /// Starts the station once: qtprogram's `Station::begin`.
    ///
    /// On first run (no `autoAttach` setting) it raises the `autoAttach` prompt
    /// and waits; `answer_auto_attach` then calls [`Self::begin_checks`].
    /// Otherwise it begins the checks at once. A second `start` is ignored.
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

    /// Runs the first checks: Programmer, Base (which may attach, and so reset
    /// the tag, when automatic attach is on), a catalog refresh, the image, and
    /// the record.
    ///
    /// With no record path saved it raises the `recordFile` prompt. With one,
    /// it sets the Record row to "Opening..." and opens the file in a
    /// `Msg::Done` posted to its own channel, so the open runs on the next pass
    /// of the loop -- after this pass has published the view. On macOS the
    /// first open of a file under Documents blocks until the user answers the
    /// system's folder-access (TCC) prompt; deferring it means the window is
    /// already drawn, with the Record row explaining, while that question is
    /// up. A failed open is logged and raises the `recordFile` prompt.
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
        if path.is_empty() {
            self.check_record();
            self.raise_record_prompt();
            return;
        }
        // Opening the record can wait on the user: macOS holds the first
        // open() in Documents until the app is allowed in. Show the window
        // first, then open it on the next pass of the engine loop.
        let name = Path::new(&path)
            .file_name()
            .map(|n| n.to_string_lossy().into_owned())
            .unwrap_or_default();
        self.set(
            Check::Record,
            Level::Pending,
            format!(
                "Opening {name}. If the system asks, allow access to the folder that holds it."
            ),
        );
        let _ = self.tx.send(Msg::Done(Box::new(move |s: &mut Station| {
            let mut record = RecordLog::new();
            match record.open(Path::new(&path)) {
                Ok(()) => s.record = Some(record),
                Err(e) => warn!("{e}"),
            }
            s.check_record();
            if s.record.is_none() {
                s.raise_record_prompt();
            }
        })));
    }

    /// The Attach Automatically setting; off when never set.
    pub(crate) fn auto_attach(&self) -> bool {
        self.settings.bool(key::AUTO_ATTACH, false)
    }

    /// Saves Attach Automatically, and turning it on attaches at once when a
    /// base is ready and no tag is attached (qtprogram's `setAutoAttach`).
    fn set_auto_attach(&mut self, on: bool) {
        self.save_setting(key::AUTO_ATTACH, on);
        if on && self.have_base && !self.attached() && self.started {
            self.attach();
        }
    }

    // ---- base --------------------------------------------------------

    /// Looks for bases on USB and chooses one: qtprogram's `checkBase`.
    ///
    /// Does nothing while busy or attached. With none it warns and arms the
    /// base poll; with one, or with the saved serial among several, it uses
    /// it; otherwise it raises the `chooseBase` prompt. Using a base may
    /// attach (see [`Self::base_ready`]).
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

    /// Raises the `chooseBase` prompt listing `bases`, with the saved base
    /// preselected. The answer's index refers to this listing; see
    /// [`Self::choose_base`].
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

    /// Station > Choose Base...: qtprogram's `listBases`. Forgets the tag and
    /// raises `chooseBase`, or an info alert while busy or with no base.
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

    /// Uses the base at `index` in a fresh USB listing, as the answer to
    /// `chooseBase`. An index past the end is ignored.
    ///
    /// The listing is taken again here, so it is assumed to be in the same
    /// order as when the prompt was raised; a base plugged or unplugged
    /// meanwhile can shift the indices.
    fn choose_base(&mut self, index: usize) {
        let bases = tagprobe::list();
        if let Some(b) = bases.get(index) {
            self.use_base(b.clone());
        }
    }

    /// Uses `base`: qtprogram's `chooseBase`, with the base's identity added.
    ///
    /// Fails the Base check for a base whose serial cannot be read while
    /// others are plugged in, since it could not be found again. Otherwise it
    /// saves the serial, sends the identify command (`F6 99`) to choose the
    /// probe driver, sets the Base row, and calls [`Self::base_ready`], which
    /// attaches when automatic attach is on.
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

    /// qtprogram's `baseReady`: attaches when automatic attach is on, else
    /// sets the SWD row to ask for Attach, warning that it resets the tag.
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

    /// The probe driver for the base: probe-rs's own ST-LINK driver for a base
    /// that answered the identify command, tagprobe's field-safe one
    /// otherwise.
    pub(crate) fn driver(&self) -> Driver {
        Driver::for_identity(self.base_identity.as_deref())
    }

    // ---- attach ------------------------------------------------------

    /// Attaches to the tag: qtprogram's `attach`. **This resets the tag.**
    ///
    /// Ignored while busy. Forgets the tag, re-finds the base on USB (it may
    /// have been replugged), marks Supply, Tag and Tag state pending, and runs
    /// the SWD connect check on a worker with [`Self::worker_busy`] set. When it
    /// returns, the SWD and Supply rows are set and, on success, the monitor
    /// attach begins ([`Self::attach_tag`]); on failure the Firmware check is
    /// re-run against what is known.
    ///
    /// If no base is known, [`Self::check_base`] runs first; with automatic
    /// attach on, that can itself call `attach` (through `use_base` and
    /// `base_ready`), after which this call carries on and starts a second
    /// connect check.
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

    /// The chosen base, re-found on USB by its saved serial, or the only base
    /// when just one is plugged in; `None` otherwise.
    pub(crate) fn find_base(&self) -> Option<UsbDev> {
        let bases = tagprobe::list();
        let saved = self.settings.string(key::BASE_SERIAL);
        bases
            .iter()
            .find(|b| !saved.is_empty() && b.serial == saved)
            .cloned()
            .or_else(|| (bases.len() == 1).then(|| bases[0].clone()))
    }

    /// Attaches the monitor and reads the tag: qtprogram's `attachTag`.
    ///
    /// On success it sets the Tag row, reads the status, detects the tag type,
    /// starts the status poll and re-checks the firmware. If nothing was
    /// attached it retries `tries - 1` more times, [`ATTACH_RETRY`] apart (the
    /// ST-LINK may still be held after the connect check). When the tries run
    /// out, or the link opened but the firmware does not answer `TagInfo`, it
    /// detaches and takes the tag for a blank board or one in its bootloader:
    /// the Tag row warns and asks for the tag type.
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

    /// Re-attaches to the base after programming or a read-back; true if the
    /// monitor link opens. **This resets the tag.**
    ///
    /// Does not ask for `TagInfo`, so true does not mean the firmware answers.
    /// Used by the session and by writing the record.
    pub(crate) fn reattach(&mut self) -> bool {
        self.tag.detach();
        match self.find_base() {
            Some(b) => self.tag.attach(&b),
            None => false,
        }
    }

    /// Stops the status poll, so a session's own status reads are the only
    /// monitor traffic.
    pub(crate) fn stop_status_poll(&mut self) {
        self.status_poll = None;
    }

    /// Restarts the status poll after a session.
    pub(crate) fn resume_status_poll(&mut self) {
        self.status_poll = Some(Instant::now() + STATUS_POLL);
    }

    /// Detaches and forgets everything about the tag: qtprogram's
    /// `forgetTag`. Stops the status poll and any attach retry. Leaves the
    /// checks as they are; the callers set them.
    pub(crate) fn forget_tag(&mut self) {
        self.status_poll = None;
        self.attach_retry = None;
        self.tag.detach();
        self.info = None;
        self.status = None;
        self.offset = None;
        self.missed = 0;
    }

    /// Sets the Supply check from the connect check's voltage: qtprogram's
    /// `checkSupply`. Above 3.6 V on an L432 fails; below 1.8 V warns.
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

    /// Reads the tag's status, and with it the clock offset; true on success.
    /// Clears the missed-poll count. On failure leaves the last status in
    /// place.
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

    /// Sets the Tag state check from the last status: qtprogram's
    /// `checkTagState`. IDLE passes; FINISHED and ABORTED warn that programming
    /// erases the log; TEST and SRESET (erasing) are pending; any other state is
    /// a deployment that programming stops and erases.
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

    /// The status poll: qtprogram's `poll`. Skipped during a session or
    /// while a worker owns the base. After [`MISSED_POLLS`] failed reads in a
    /// row the tag is forgotten and the user asked to attach again.
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

    /// Next Tag: qtprogram's `nextTag`. Ignored while busy. Forgets the tag,
    /// the connect check, the session and its result, and asks the user to
    /// swap the tag and press Attach. Does not attach.
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

    /// The Tag state row's Stop: sends the monitor's stop and re-reads the
    /// state. A refusal is silent.
    fn stop_tag(&mut self) {
        if self.attached() && self.tag.stop().is_ok() {
            self.read_status();
            self.check_tag_state();
        }
    }

    /// The Tag state row's Erase: asks the tag's own firmware to erase its
    /// log, only from FINISHED or ABORTED (the UI confirms first), then
    /// re-reads the state. The erase runs on the tag; this returns as soon as
    /// it is accepted, and the poll shows SRESET until it ends. A refusal is
    /// silent.
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

    /// Sync: sets the tag's RTC to the host clock (qtprogram's
    /// `syncClock`), alerting if the tag refuses, then re-reads the offset.
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

    /// Lists the releases on GitHub on a worker (Station > Refresh Firmware
    /// Releases, and at start-up). One at a time. When it finishes the catalog
    /// is reloaded and, if the chosen release has no image loaded yet, the
    /// image is loaded (which may start a download).
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
                    s.reload_for_catalog();
                } else {
                    s.check_firmware();
                }
            },
        );
    }

    /// Chooses a release: qtprogram's `chooseRelease`. Saves it, forgets any
    /// chosen file, and loads its image for the tag type (downloading it if
    /// need be).
    fn choose_release(&mut self, tag: &str) {
        self.save_setting(key::RELEASE, tag);
        self.remove_setting(key::FILE);
        self.remove_setting(key::LEGACY_IMAGE);
        self.image = None;
        self.reload_for_catalog();
    }

    /// Detects the tag type against the chosen release's targets, which
    /// change as releases are chosen and downloaded, then loads the image if
    /// detection did not.
    fn reload_for_catalog(&mut self) {
        self.detect_target();
        if self.image.is_none() {
            self.load_image();
        }
    }

    /// Chooses a local ELF: qtprogram's `chooseFile`. Saves it as the
    /// firmware, forgets the release, adds it to the recent files and loads it.
    /// An empty path (a cancelled file dialog) is ignored.
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

    /// Chooses the tag type by hand: qtprogram's `chooseTarget`. Saves it,
    /// clears the detected flag and loads the image.
    fn choose_target(&mut self, name: &str) {
        self.target = name.to_owned();
        self.target_detected = false;
        self.save_setting(key::TARGET, name);
        self.load_image();
    }

    /// The chosen release's tag, if a release is the firmware.
    pub(crate) fn chosen_release(&self) -> Option<String> {
        Some(self.settings.string(key::RELEASE)).filter(|s| !s.is_empty())
    }

    /// The chosen local ELF, if a file (and not a release) is the firmware.
    pub(crate) fn chosen_file(&self) -> Option<String> {
        Some(self.settings.firmware_file())
            .filter(|s| !s.is_empty())
            .filter(|_| self.chosen_release().is_none())
    }

    /// Loads the chosen firmware: qtprogram's `loadImage`, then re-checks it.
    ///
    /// A file is loaded directly, and its identity record sets the tag type. A
    /// release that is not cached starts a download when it has an archive
    /// (the image loads when that ends); a cached one loads the image for the
    /// current tag type, if the release has one. Reading the ELF and hashing
    /// it happen here, on the engine thread.
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

    /// Downloads, verifies and installs a release on a worker. One at a time:
    /// a second call while one runs is ignored. When it ends, a failure is kept
    /// for the Firmware check, and the image is reloaded if the release is
    /// still the chosen one.
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
                    s.image = None;
                    s.reload_for_catalog();
                }
            }
        });
    }

    /// Sets the tag type from the attached tag's source path when the chosen
    /// release has that target: qtprogram's `detectTarget`. Loads the image if
    /// the type changed or none is loaded. Does nothing for a file, a blank
    /// board, or a target the release lacks.
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

    /// Sets the Firmware check: qtprogram's `checkFirmware`, in its order.
    ///
    /// Fails when nothing is chosen, when the release is not downloaded (with
    /// why: downloading, looking up, unreachable, unpublished, or the download
    /// error), when it has no image for the tag type, when the image has an
    /// error, or when its page size does not match the connected part. Warns
    /// when the image is for a different target than the tag runs, when the
    /// image has a warning, or when it is not a qualified release for the
    /// type. Otherwise passes, saying whether it was verified against a
    /// manifest.
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

    /// The target the attached tag runs, from the last component of its source
    /// path; `None` for no tag or an empty path.
    pub(crate) fn tag_target(&self) -> Option<String> {
        let info = self.info.as_ref()?;
        let t = info.source_path.rsplit('/').find(|p| !p.is_empty())?;
        Some(t.to_owned())
    }

    /// qtprogram's `qualificationWarning`: why the image is not known to be
    /// qualified for its target -- a local file, a release not qualified for
    /// it, or a release GitHub has not listed -- or `None` when it is.
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

    /// qtprogram's `defaultRecordPath`:
    /// `<Documents>/Tag Programmer/programmed-tags.csv`.
    pub(crate) fn default_record_path(&self) -> PathBuf {
        self.cfg
            .documents_dir
            .join("Tag Programmer")
            .join("programmed-tags.csv")
    }

    /// Raises the `recordFile` prompt, offering the default path and whether
    /// it exists.
    fn raise_record_prompt(&mut self) {
        let path = self.default_record_path();
        self.raise(json!({
            "kind": "recordFile",
            "defaultPath": path.to_string_lossy(),
            "exists": path.exists(),
        }));
    }

    /// Opens an existing record and saves its path: qtprogram's `openRecord`.
    /// A failure raises an alert and leaves the previous record in place.
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

    /// Creates a new record with its header and saves its path: qtprogram's
    /// `createRecord`. A failure raises an alert.
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

    /// Sets the Record check: qtprogram's `checkRecord`. Fails with no record
    /// or when it is not writable (re-tested on every call); otherwise passes
    /// with the file name and row count.
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
    ///
    /// `Err` while a session or a worker runs or an attach is in progress; when
    /// Programmer, Base, SWD or Record is not OK; when Supply fails; or when
    /// Firmware fails or is pending, or the image is not usable. Its text is
    /// the Program button's tooltip and the Program dialog's refusal. A blank
    /// board (Tag warning) may be programmed.
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

    /// The tag carries calibration constants in flash (a CompassTag or
    /// IMUTag), from the attached tag's type or, for a blank board, the
    /// image's.
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

    /// The `program_preview` query: what the Program dialog shows -- the tag,
    /// the image, the erase, what is lost and kept, and notes -- or
    /// `{ ok: false, reason }` when [`Self::can_program`] refuses.
    ///
    /// # Panics
    ///
    /// Never in practice: `can_program` succeeding implies a usable image.
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

    /// The `label_notes` query: warnings about the label as typed -- that it
    /// is recorded for another tag, or that this tag is recorded under other
    /// labels. Empty without a record or an attached tag.
    fn label_notes(&self, label: &str) -> Vec<String> {
        let label = label.trim();
        let mut notes = Vec::new();
        let (Some(record), Some(uuid)) = (&self.record, self.info.as_ref().map(|i| i.uuid.clone()))
        else {
            return notes;
        };
        if let Some(conflict) = self.label_conflict(label, &uuid) {
            notes.push(conflict);
        }
        let labels = record.labels_for_uuid(&uuid);
        if !labels.is_empty() && !labels.iter().any(|l| l == label) {
            notes.push(format!("This tag is recorded as {}.", labels.join(", ")));
        }
        notes
    }

    /// The warning when `label` is already recorded for a tag other than
    /// `uuid`: two boards sharing a label make the record ambiguous. `None`
    /// when the label is empty, unused, or recorded only for `uuid`, or when
    /// no record is open.
    ///
    /// For a blank board `uuid` is empty, so every recorded use of the label
    /// counts. The UI asks this after the Program or Retest dialog and makes
    /// the user confirm (a port difference: qtprogram only noted it).
    pub(crate) fn label_conflict(&self, label: &str, uuid: &str) -> Option<String> {
        let label = label.trim();
        let record = self.record.as_ref()?;
        if label.is_empty() {
            return None;
        }
        let others: Vec<String> = record
            .uuids_for_label(label)
            .into_iter()
            .filter(|u| u != uuid)
            .collect();
        (!others.is_empty()).then(|| {
            format!(
                "{label} is already recorded for {} {}.",
                if others.len() == 1 {
                    "another tag, UUID"
                } else {
                    "other tags, UUIDs"
                },
                others.join(", ")
            )
        })
    }

    /// The `retest_default_label` query: the last label recorded for the
    /// attached tag's UUID, or empty.
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

    /// The erase that goes with the chosen image, for tagprobe: a mass erase,
    /// the pages below the calibration page (keeping calibration and what
    /// lies above), or only the pages the image covers.
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

    /// The part's internal flash in bytes: what the tag reports, else what
    /// a blank board's image was linked for. A UIUCTag is an STM32L432KB
    /// (128 KB) running an image linked for the 256 KB KC.
    ///
    /// `None` when neither is known; reads the ELF from disk for a blank
    /// board.
    fn flash_bytes(&self, image: &FirmwareImage) -> Option<u32> {
        let reported = self
            .info
            .as_ref()
            .map(|i| i.intflashsz * 1024)
            .filter(|&b| b > 0)
            .and_then(|b| u32::try_from(b).ok());
        reported.or_else(|| {
            crate::model::elf::Elf::load(&image.elf_path)
                .ok()?
                .symbol("__flash0_size__")
        })
    }

    /// The probe-rs target for the attached part and the image: the SWD device
    /// id when connected, else the family the image's page size implies, sized
    /// by [`Self::flash_bytes`].
    ///
    /// # Errors
    ///
    /// tagprobe's message when no probe-rs target fits the part and size.
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
        tagprobe::chip_for(device, self.flash_bytes(image))
    }

    /// Sets a setting and saves the file, logging a failure to save it.
    pub(crate) fn save_setting(&mut self, name: &str, value: impl Into<Value>) {
        if let Err(e) = self.settings.set(name, value) {
            warn!("{e}");
        }
    }

    /// Removes a setting and saves the file, logging a failure.
    fn remove_setting(&mut self, name: &str) {
        if let Err(e) = self.settings.remove(name) {
            warn!("{e}");
        }
    }
}
