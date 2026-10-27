//! The engine thread's state and behaviour: qtmonitor's `MainWindow` (its
//! commands and its handling of the tag session's reports) and the document
//! side of its `ConfigTab`, with every dialog turned into a prompt.
//!
//! `Monitor` never touches the tag: it sends [`TagCmd`]s to the tag thread
//! and applies the [`Report`]s that come back. Where qtmonitor opened a
//! modal dialog and waited, the monitor queues a [`Prompt`] and continues
//! when the UI answers it (`answer`), carrying what was to happen next as an
//! [`After`].

use std::collections::VecDeque;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::Sender;
use std::time::{Duration, Instant};

use log::{debug, info, warn};
use serde_json::{Value, json};
use tagcore::UsbDev;
use tagcore::configshape::ConfigShape;
use tagcore::proto::{Config, TagInfo, TagState, TagType};

use super::download::{self, DEFAULT_DOWNLOAD_NAME, DownloadResult};
use super::tagthread::{Report, TagCmd, now_ms};
use super::{query_error, set_log_level};
use crate::model::commands::{ALL_COMMANDS, Availability, Command, availability, command_name};
use crate::model::configdocument::{
    ConfigDocument, DocumentSource, OnAttach, ParseFileError, on_attach,
};
use crate::model::monitormodel::{MonitorEvent, MonitorModel, NO_ANSWER_MS, TEST_TIMEOUT_MS};
use crate::model::startchecks::{start_check_failures, start_check_message};
use crate::model::tagrules::{REQUIRED_MONITOR_VERSION, monitor_version_accepted};

/// Tag Monitor's version, as qtmonitor's `MainWindow::version`.
pub(crate) const VERSION: f32 = 2.1;

/// How often the view is redrawn while a tag is attached, so a tag that has
/// stopped answering is noticed (qtmonitor's silence timer).
const TICK: Duration = Duration::from_secs(1);

/// The settings key of the recent configuration files, as qtmonitor's.
const RECENT_KEY: &str = "recentConfigurationFiles";
/// Most recent configuration files remembered.
const RECENT_MAX: usize = 10;

/// What happens once the user has saved or discarded unsaved changes.
#[derive(Debug, Clone)]
pub(crate) enum After {
    /// Read Configuration.
    Read,
    /// Open Configuration...: ask for the file.
    Open,
    /// Open this file (Open Recent).
    OpenFile(String),
    /// New Configuration from this tag type's defaults.
    New(TagType),
    /// Make the attached tag's configuration the document.
    AttachConfig(Box<Config>),
    /// Quit.
    Quit,
}

/// A question or message for the UI, which it shows once per id.
#[derive(Debug, Clone)]
pub(crate) enum PromptKind {
    /// A message box with only OK.
    Alert {
        /// Window title.
        title: String,
        /// The main sentence.
        text: String,
        /// Further text, or empty.
        informative: String,
        /// Text behind Show Details, or empty.
        detail: String,
        /// `"warning"`, `"error"` or `"info"`.
        style: &'static str,
    },
    /// Several bases: which one to attach through.
    ChooseBase(Vec<UsbDev>),
    /// Start checks failed: start anyway?
    StartAnyway(Vec<String>),
    /// Erase the tag's state and data?
    ConfirmErase,
    /// The configuration has unsaved changes: save, discard or cancel.
    Unsaved(After),
    /// Ask for a configuration file to open.
    OpenConfig,
    /// Ask where to save the configuration; then continue with the `After`.
    SaveConfig(Option<After>),
    /// Ask where to save the download.
    SaveDownload,
    /// The file at this path names no tag type: which is it for?
    ChooseTagType(String, Vec<TagType>),
}

/// A prompt and the id the UI answers it by.
#[derive(Debug, Clone)]
pub(crate) struct Prompt {
    /// Unique within this run; a stale answer names an old one.
    pub id: u64,
    /// What to ask.
    pub kind: PromptKind,
}

/// Everything the engine thread knows.
pub(crate) struct Monitor {
    /// Files and Help.
    pub(super) config: EngineConfig,
    /// Commands for the tag thread.
    tag_tx: Sender<TagCmd>,
    /// Set to cancel a running download after its page.
    cancel: Arc<AtomicBool>,
    /// The tag as the model knows it.
    pub(super) model: MonitorModel,
    /// The last attached tag's identity, which the Tag page keeps showing
    /// (greyed) after a detach, as qtmonitor's labels did.
    pub(super) shown_info: Option<TagInfo>,
    /// Attaching: bases are being listed or the tag thread is attaching.
    pub(super) attaching: bool,
    /// The configuration document.
    pub(super) doc: ConfigDocument,
    /// The values the form holds, while there is a document. Phase 2 has no
    /// editor, so these change only by loading or Reset to Defaults.
    pub(super) form: Option<Config>,
    /// Questions waiting for the UI; the first is shown.
    pub(super) prompts: VecDeque<Prompt>,
    /// The next prompt id.
    next_id: u64,
    /// The recent configuration files, newest first.
    pub(super) recent: Vec<String>,
    /// The erase progress is shown (its Close only hides it).
    pub(super) erase_shown: bool,
    /// The download progress: records, record count; `None` when hidden.
    pub(super) download: Option<(i32, i32)>,
    /// Seconds without an answer from the tag, when at least
    /// [`NO_ANSWER_MS`]; else 0. Updated by the tick.
    pub(super) silence_s: i64,
    /// When the tick next fires, while attached.
    next_tick: Option<Instant>,
    /// Quit was confirmed; the loop sends [`super::Event::Exit`].
    exit: bool,
    /// Quit was confirmed and the tag is being detached first.
    quitting: bool,
    /// `ready` has been received once.
    started: bool,
    /// The page asked for the whole state again.
    republish: bool,
}

use super::EngineConfig;

impl Monitor {
    /// A monitor with no tag and no document; reads the recent files.
    pub(crate) fn new(
        config: EngineConfig,
        tag_tx: Sender<TagCmd>,
        cancel: Arc<AtomicBool>,
    ) -> Self {
        let recent = read_settings(&config.settings_path)
            .get(RECENT_KEY)
            .and_then(Value::as_array)
            .map(|a| {
                a.iter()
                    .filter_map(|v| v.as_str().map(String::from))
                    .collect()
            })
            .unwrap_or_default();
        Monitor {
            config,
            tag_tx,
            cancel,
            model: MonitorModel::new(),
            shown_info: None,
            attaching: false,
            doc: ConfigDocument::default(),
            form: None,
            prompts: VecDeque::new(),
            next_id: 1,
            recent,
            erase_shown: false,
            download: None,
            silence_s: 0,
            next_tick: None,
            exit: false,
            quitting: false,
            started: false,
            republish: false,
        }
    }

    /// The page is listening (the `ready` command): the view state is sent
    /// again, and the first time, the engine attaches unless the
    /// configuration says not to, as qtmonitor does when its window opens.
    fn ready(&mut self) {
        self.republish = true;
        if !std::mem::replace(&mut self.started, true) && self.config.auto_attach {
            self.attach();
        }
    }

    /// True once after `ready`: the loop forgets the last state it sent, so
    /// a page that reloaded gets the whole state again.
    pub(crate) fn take_republish(&mut self) -> bool {
        std::mem::take(&mut self.republish)
    }

    /// Sends a command to the tag thread.
    fn send(&self, cmd: TagCmd) {
        let _ = self.tag_tx.send(cmd);
    }

    /// Queues a prompt.
    fn ask(&mut self, kind: PromptKind) {
        let id = self.next_id;
        self.next_id += 1;
        self.prompts.push_back(Prompt { id, kind });
    }

    /// Queues a message box.
    fn alert(
        &mut self,
        title: &str,
        text: &str,
        informative: &str,
        detail: &str,
        style: &'static str,
    ) {
        self.ask(PromptKind::Alert {
            title: title.into(),
            text: text.into(),
            informative: informative.into(),
            detail: detail.into(),
            style,
        });
    }

    /// The attached tag as the log names it: `"BITTAG <uuid>"`.
    fn tag_name(&self) -> String {
        format!(
            "{} {}",
            self.model.tag_type().as_str_name(),
            self.model.info().uuid
        )
    }

    /// Whether `command` is available now, by the core rule.
    pub(super) fn available(&self, command: Command) -> Availability {
        availability(
            command,
            &self
                .model
                .command_inputs(self.doc.has_document(), self.doc.tag_type()),
        )
    }

    // ---- Timers ----

    /// The nearest deadline: the tick, while attached.
    pub(crate) fn next_deadline(&self) -> Option<Instant> {
        self.next_tick
    }

    /// Fires the tick if due: recomputes how long the tag has been silent.
    pub(crate) fn fire_timers(&mut self, now: Instant) {
        if !self.model.attached() {
            self.next_tick = None;
            self.silence_s = 0;
            return;
        }
        if self.next_tick.is_none_or(|t| now >= t) {
            let ms = self.model.silence_ms(now_ms());
            self.silence_s = if ms >= NO_ANSWER_MS { ms / 1000 } else { 0 };
            self.next_tick = Some(now + TICK);
        }
    }

    /// True once after Quit was confirmed.
    pub(crate) fn take_exit(&mut self) -> bool {
        std::mem::take(&mut self.exit)
    }

    // ---- The document ----

    /// What the form would send now; `None` with no document.
    fn collect(&self) -> Option<Config> {
        self.form.clone()
    }

    /// Whether the document has unsaved changes.
    pub(super) fn is_modified(&self) -> bool {
        self.doc.is_modified(self.collect().as_ref())
    }

    /// The document's name for the title: `"BITTAG defaults"`, a file name,
    /// `"BITTAG from tag"`, or empty.
    pub(super) fn document_name(&self) -> String {
        let tag_type = self.doc.tag_type().as_str_name();
        match self.doc.source() {
            DocumentSource::None => String::new(),
            DocumentSource::Defaults => format!("{tag_type} defaults"),
            DocumentSource::File => file_name(self.doc.path()),
            DocumentSource::Tag => format!("{tag_type} from tag"),
        }
    }

    /// Loads `config` as the document (ConfigTab::load). A reset keeps the
    /// old baseline, so it shows as unsaved changes.
    fn load(&mut self, config: Config, source: DocumentSource, path: &str, keep_baseline: bool) {
        self.doc.load(config.clone(), source, path);
        self.form = Some(config);
        if !keep_baseline {
            let written = self.collect();
            self.doc.set_baseline(written.as_ref());
        }
        self.doc.content_changed();
    }

    /// Runs `after` now if nothing is unsaved, else asks first
    /// (ConfigTab::confirmDiscard).
    fn confirm_discard(&mut self, after: After) {
        if self.is_modified() {
            self.ask(PromptKind::Unsaved(after));
        } else {
            self.then(after);
        }
    }

    /// Continues with `after`, unsaved changes having been dealt with.
    fn then(&mut self, after: After) {
        match after {
            After::Read => self.send(TagCmd::ReadConfig),
            After::Open => self.ask(PromptKind::OpenConfig),
            After::OpenFile(path) => self.open_file(&path),
            After::New(tag_type) => {
                if let Some(defaults) = ConfigShape::default_config(tag_type) {
                    self.load(defaults, DocumentSource::Defaults, "", false);
                }
            }
            After::AttachConfig(config) => self.load(*config, DocumentSource::Tag, "", false),
            // Detach first, as qtmonitor's session does when it closes; the
            // exit follows the tag thread's Detached.
            After::Quit if self.model.attached() || self.attaching => {
                self.quitting = true;
                self.cancel.store(true, Ordering::Relaxed);
                self.send(TagCmd::Detach);
            }
            After::Quit => self.exit = true,
        }
    }

    /// Opens a configuration file, asking for its tag type if it names none
    /// (ConfigTab::OpenFile, after confirmDiscard).
    fn open_file(&mut self, path: &str) {
        let text = match std::fs::read_to_string(path) {
            Ok(text) => text,
            Err(_) => {
                return self.alert(
                    "Open Configuration",
                    &format!("Couldn't open {path}"),
                    "",
                    "",
                    "warning",
                );
            }
        };
        if ConfigDocument::names_no_tag_type(&text) {
            // Offer the attached tag's type first.
            let mut types = ConfigShape::tag_types();
            let attached = if self.model.attached() {
                self.model.tag_type()
            } else {
                TagType::TagUnspecified
            };
            types.sort_by_key(|t| *t != attached);
            self.ask(PromptKind::ChooseTagType(path.to_string(), types));
            return;
        }
        self.open_text(path, &text, TagType::TagUnspecified);
    }

    /// Loads the file's text onto its tag type's defaults, or says why not.
    fn open_text(&mut self, path: &str, text: &str, fallback: TagType) {
        match ConfigDocument::parse_file(text, fallback) {
            Ok(config) => {
                let full = absolute(path);
                self.load(config, DocumentSource::File, &full, false);
                self.add_recent(&full);
            }
            Err(ParseFileError::Parse(detail)) => self.alert(
                "Open Configuration",
                &format!("Couldn't read {path}\n\n{detail}"),
                "",
                "",
                "warning",
            ),
            Err(ParseFileError::NoDefaults(detail)) => self.alert(
                "Open Configuration",
                &format!("Couldn't open {path}\n\n{detail}"),
                "",
                "",
                "warning",
            ),
        }
    }

    /// Writes the configuration to `path`; true when it was written
    /// (ConfigTab::writeFile).
    fn write_file(&mut self, path: &str) -> bool {
        let Some(config) = self.collect() else {
            self.alert(
                "Save Configuration",
                "The configuration cannot be written: a setting has no valid value.",
                "",
                "",
                "warning",
            );
            return false;
        };
        if std::fs::write(path, ConfigDocument::file_text(&config)).is_err() {
            self.alert(
                "Save Configuration",
                &format!("Couldn't write {path}"),
                "",
                "",
                "warning",
            );
            return false;
        }
        let full = absolute(path);
        self.doc.saved(&full, config);
        self.add_recent(&full);
        true
    }

    /// The path Save suggests: the document's file, or
    /// `~/<tagtype>-config.json`.
    pub(super) fn suggested_config_path(&self) -> String {
        if !self.doc.path().is_empty() {
            return self.doc.path().to_string();
        }
        let name = format!(
            "{}-config.json",
            self.doc.tag_type().as_str_name().to_lowercase()
        );
        self.config
            .home_dir
            .join(name)
            .to_string_lossy()
            .into_owned()
    }

    /// The folder Open starts in: the document's, or home.
    pub(super) fn open_start_dir(&self) -> String {
        Path::new(self.doc.path())
            .parent()
            .filter(|p| !p.as_os_str().is_empty())
            .unwrap_or(&self.config.home_dir)
            .to_string_lossy()
            .into_owned()
    }

    /// Puts `path` first in the recent files and saves the list.
    fn add_recent(&mut self, path: &str) {
        self.recent.retain(|p| p != path);
        self.recent.insert(0, path.to_string());
        self.recent.truncate(RECENT_MAX);
        let mut settings = read_settings(&self.config.settings_path);
        settings[RECENT_KEY] = json!(self.recent);
        if let Some(dir) = self.config.settings_path.parent() {
            let _ = std::fs::create_dir_all(dir);
        }
        if let Err(e) = std::fs::write(&self.config.settings_path, settings.to_string()) {
            warn!(
                "Could not save the recent files to {}: {e}",
                self.config.settings_path.display()
            );
        }
    }

    // ---- Commands ----

    /// Applies one UI command; see the contract's command table.
    pub(crate) fn command(&mut self, cmd: &str, args: &Value) {
        if let Some(&command) = ALL_COMMANDS.iter().find(|c| command_name(**c) == cmd) {
            if self.available(command) == Availability::Enabled {
                self.run_command(command);
            } else {
                debug!("{cmd}: not available now");
            }
            return;
        }
        let text = |key: &str| {
            args.get(key)
                .and_then(Value::as_str)
                .unwrap_or_default()
                .to_string()
        };
        match cmd {
            "ready" => self.ready(),
            "answer" => self.answer(args),
            "new_config" => {
                if let Some(t) = TagType::from_str_name(&text("tagType")) {
                    self.confirm_discard(After::New(t));
                }
            }
            "open_recent" => self.confirm_discard(After::OpenFile(text("path"))),
            "set_approved" => {
                let on = args.get("on").and_then(Value::as_bool).unwrap_or(false);
                self.doc.set_approved(on);
            }
            "cancel_download" => self.cancel.store(true, Ordering::Relaxed),
            "hide_erase" => self.erase_shown = false,
            "quit" => self.confirm_discard(After::Quit),
            "set_log_level" => set_log_level(&text("level")),
            "save_text" => {
                let path = text("path");
                if let Err(e) = std::fs::write(&path, text("text")) {
                    self.alert(
                        "Save Error Log",
                        &format!("Couldn't write {path}: {e}"),
                        "",
                        "",
                        "warning",
                    );
                }
            }
            _ => warn!("Unknown command {cmd}"),
        }
    }

    /// Runs an available command (MainWindow::runCommand).
    fn run_command(&mut self, command: Command) {
        match command {
            Command::Attach => self.attach(),
            Command::Detach => self.send(TagCmd::Detach),
            Command::Read => self.confirm_discard(After::Read),
            Command::Start => self.start_with_checks(),
            Command::Stop => self.send(TagCmd::Stop),
            Command::Sync => self.send(TagCmd::SyncClock),
            Command::Test => {
                if self.model.state() != TagState::Idle {
                    return;
                }
                self.send(TagCmd::SelfTest);
                self.model.test_sent(now_ms());
                info!("Self-test started on {}", self.tag_name());
            }
            Command::Erase => self.ask(PromptKind::ConfirmErase),
            Command::Download => self.ask(PromptKind::SaveDownload),
            Command::Open => self.confirm_discard(After::Open),
            Command::Save => self.ask(PromptKind::SaveConfig(None)),
            Command::Reset => {
                if let Some(defaults) = ConfigShape::default_config(self.doc.tag_type()) {
                    let (source, path) = (self.doc.source(), self.doc.path().to_string());
                    self.load(defaults, source, &path, true);
                }
            }
        }
    }

    /// Attach: lists the bases, unless already attached or attaching.
    fn attach(&mut self) {
        if self.model.attached() {
            return self.alert("Warning", "Already Attached to Tag", "", "", "warning");
        }
        if !self.attaching {
            self.attaching = true;
            self.send(TagCmd::FindBases);
        }
    }

    /// The failing start checks' sentences.
    fn start_check_failures(&self) -> Vec<String> {
        let (offset, test) = (self.model.clock_offset_s(), self.model.last_test());
        start_check_failures(offset, test, self.doc.approved())
            .into_iter()
            .map(|c| start_check_message(c, offset, test))
            .collect()
    }

    /// Start: asks first if a check fails.
    fn start_with_checks(&mut self) {
        let failures = self.start_check_failures();
        if failures.is_empty() {
            self.start();
        } else {
            self.ask(PromptKind::StartAnyway(failures));
        }
    }

    /// Sends the form's configuration to start the tag.
    fn start(&mut self) {
        match self.collect() {
            Some(config) => {
                self.doc.set_config(config.clone());
                self.send(TagCmd::Start(Box::new(config)));
            }
            None => debug!("Start: the configuration cannot be written"),
        }
    }

    // ---- Prompts ----

    /// The UI's answer to the shown prompt: `{id, button, index?, path?}`.
    /// An answer to any other id is stale and ignored.
    fn answer(&mut self, args: &Value) {
        let id = args.get("id").and_then(Value::as_u64);
        if id.is_none() || self.prompts.front().map(|p| p.id) != id {
            return debug!("Ignoring a stale answer");
        }
        let prompt = self.prompts.pop_front().expect("front checked");
        let button = args
            .get("button")
            .and_then(Value::as_str)
            .unwrap_or("cancel");
        let index = args
            .get("index")
            .and_then(Value::as_u64)
            .map(|i| i as usize);
        let path = args
            .get("path")
            .and_then(Value::as_str)
            .filter(|p| !p.is_empty());
        let ok = button == "ok";
        match prompt.kind {
            PromptKind::Alert { .. } => {}
            PromptKind::ChooseBase(bases) => match (ok, index.and_then(|i| bases.get(i))) {
                (true, Some(base)) => self.send(TagCmd::Attach(base.clone())),
                _ => self.attaching = false,
            },
            PromptKind::StartAnyway(failures) => {
                if button == "start" {
                    warn!(
                        "Starting {} although: {}",
                        self.tag_name(),
                        failures.join(" ")
                    );
                    self.start();
                }
            }
            PromptKind::ConfirmErase => {
                if ok {
                    self.send(TagCmd::Erase);
                }
            }
            PromptKind::Unsaved(after) => match button {
                "save" => self.ask(PromptKind::SaveConfig(Some(after))),
                "discard" => self.then(after),
                _ => {}
            },
            PromptKind::OpenConfig => {
                if let (true, Some(path)) = (ok, path) {
                    self.open_file(path);
                }
            }
            PromptKind::SaveConfig(after) => {
                if let (true, Some(path)) = (ok, path)
                    && self.write_file(path)
                    && let Some(after) = after
                {
                    self.then(after);
                }
            }
            PromptKind::SaveDownload => {
                if let (true, Some(path)) = (ok, path) {
                    let path: PathBuf = download::download_path(Path::new(path));
                    self.model.download_started();
                    self.download = Some((0, 0));
                    self.send(TagCmd::Download(path));
                }
            }
            PromptKind::ChooseTagType(path, types) => {
                if let (true, Some(&t)) = (ok, index.and_then(|i| types.get(i))) {
                    match std::fs::read_to_string(&path) {
                        Ok(text) => self.open_text(&path, &text, t),
                        Err(_) => self.alert(
                            "Open Configuration",
                            &format!("Couldn't open {path}"),
                            "",
                            "",
                            "warning",
                        ),
                    }
                }
            }
        }
    }

    // ---- Reports from the tag thread ----

    /// Applies one report (MainWindow's `on...` slots).
    pub(crate) fn report(&mut self, report: Report) {
        match report {
            Report::BasesFound(bases) => match bases.len() {
                0 => {
                    self.attaching = false;
                    self.alert("Warning", "No Tag Bases Found", "", "", "warning");
                }
                1 => self.send(TagCmd::Attach(bases[0].clone())),
                _ => self.ask(PromptKind::ChooseBase(bases)),
            },
            Report::Attached(info, config) => {
                self.attaching = false;
                if !monitor_version_accepted(VERSION) {
                    self.alert(
                        "Warning",
                        &format!("monitor version {VERSION} less than required version {REQUIRED_MONITOR_VERSION}"),
                        "",
                        "",
                        "warning",
                    );
                    return self.send(TagCmd::Detach);
                }
                self.shown_info = Some((*info).clone());
                self.model.set_attached(*info);
                self.next_tick = Some(Instant::now() + TICK);
                match on_attach(self.doc.source()) {
                    OnAttach::Keep => {}
                    OnAttach::ReplaceIfDiscarded => {
                        self.confirm_discard(After::AttachConfig(config))
                    }
                    OnAttach::Replace => self.then(After::AttachConfig(config)),
                }
            }
            Report::AttachFailed(message) => {
                self.attaching = false;
                self.alert("Warning", &message, "", "", "warning");
            }
            Report::Detached => {
                self.model.set_detached();
                self.erase_shown = false;
                self.download = None;
                self.silence_s = 0;
                self.exit |= std::mem::take(&mut self.quitting);
            }
            Report::Status(status, voltage, host_ms) => {
                let events = self.model.apply_status(*status, voltage, host_ms);
                self.silence_s = 0;
                for event in events {
                    self.monitor_event(event);
                }
            }
            Report::ConfigRead(config) => self.load(*config, DocumentSource::Tag, "", false),
            Report::StartSent(message) => self.model.start_sent(now_ms(), message),
            Report::TestSent => {}
            Report::EraseSent => {
                self.model.erase_sent();
                self.erase_shown = true;
            }
            Report::CommandFailed(command, message) => self.command_failed(command, &message),
            Report::DownloadProgress(value, maximum) => {
                if self.download.is_some() {
                    self.download = Some((value, maximum));
                }
            }
            Report::DownloadFinished(result) => self.download_finished(result),
        }
    }

    /// Logs or shows what a status settled.
    fn monitor_event(&mut self, event: MonitorEvent) {
        match event {
            MonitorEvent::TagLogLine(text) => debug!("Log: {text}"),
            MonitorEvent::TestFinished(result) => {
                info!(
                    "Self-test on {} finished: {}",
                    self.tag_name(),
                    result.as_str_name()
                );
            }
            MonitorEvent::TestTimedOut => warn!(
                "Self-test on {} gave no result within {} s",
                self.tag_name(),
                TEST_TIMEOUT_MS / 1000
            ),
            MonitorEvent::StartSucceeded { state, message } => {
                // The tag started although the call reported an error.
                if !message.is_empty() {
                    warn!(
                        "Start() reported a transient failure but the tag reached {} : {message}",
                        state.as_str_name()
                    );
                }
            }
            MonitorEvent::StartFailed { state, message } => {
                let last = format!("Last known state: {}", state.as_str_name());
                if message.is_empty() {
                    self.alert("Error", "Start Failed", &last, "", "error");
                } else {
                    self.alert(
                        "Error",
                        "Start Failed",
                        &format!("{message}\n{last}"),
                        &message,
                        "error",
                    );
                }
            }
            MonitorEvent::EraseFinished => self.erase_shown = false,
        }
    }

    /// Shows a refused command (MainWindow::onCommandFailed).
    fn command_failed(&mut self, command: Command, message: &str) {
        match command {
            Command::Read => self.alert("Error", message, "", "", "error"),
            Command::Sync => self.alert(
                "Sync Failed",
                "The tag did not accept the clock sync command.",
                message,
                message,
                "warning",
            ),
            Command::Stop => self.alert("Stop Failed", message, "", "", "warning"),
            Command::Test => {
                self.model.test_refused();
                warn!("Self-test on {} was refused: {message}", self.tag_name());
                self.alert("Self-Test Failed", message, "", "", "warning");
            }
            Command::Erase => self.alert("Erase Failed", message, "", "", "warning"),
            _ => self.alert("Warning", message, "", "", "warning"),
        }
    }

    /// The download ended: hides its progress, and says why if it failed.
    fn download_finished(&mut self, result: DownloadResult) {
        self.model.download_ended();
        self.download = None;
        if !result.ok {
            self.alert("Download Failed", &result.error, "", "", "error");
        }
    }

    // ---- Queries ----

    /// Answers one UI query; see the contract's query table.
    pub(crate) fn query(&self, name: &str, _args: &Value) -> Value {
        match name {
            "menus" => super::view::menus_json(),
            "about" => json!({
                "title": "About Tag Monitor",
                "text": format!(
                    "Tag Monitor v{VERSION}\n\nAttach to a tag through its USB base, check it, \
                     configure and start it, and download its data."
                ),
            }),
            "user_guide_url" => json!(self.config.user_guide_url),
            "default_download_name" => json!(
                self.config
                    .home_dir
                    .join(DEFAULT_DOWNLOAD_NAME)
                    .to_string_lossy()
            ),
            _ => query_error(&format!("unknown query {name}")),
        }
    }
}

/// The settings file's JSON object; empty when it is missing or unreadable.
fn read_settings(path: &Path) -> Value {
    std::fs::read_to_string(path)
        .ok()
        .and_then(|t| serde_json::from_str::<Value>(&t).ok())
        .filter(Value::is_object)
        .unwrap_or_else(|| json!({}))
}

/// `path` made absolute against the working directory, as text.
fn absolute(path: &str) -> String {
    std::path::absolute(path)
        .map(|p| p.to_string_lossy().into_owned())
        .unwrap_or_else(|_| path.to_string())
}

/// The last component of `path`.
pub(super) fn file_name(path: &str) -> String {
    Path::new(path)
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default()
}
