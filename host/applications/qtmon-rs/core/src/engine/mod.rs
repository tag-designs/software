//! The engine: qtmonitor's `MainWindow` and `LiveTagSession`, with no UI.
//!
//! [`spawn`] starts the engine and returns an [`Engine`] handle. The UI talks
//! to it only through [`Engine::command`] and [`Engine::query`], and hears
//! from it only through the [`Event`] sink: the whole view state after every
//! change, log lines, and the request to exit. The protocol is
//! `design/ui-engine-contract.md`.
//!
//! # Threading
//!
//! Two threads, as in qtmonitor after its restructure
//! (`host/applications/qtmon/design/architecture.md`):
//!
//! - The **engine thread** (`run`) owns all state, in one `Monitor`: the
//!   [`MonitorModel`](crate::model::monitormodel::MonitorModel), the
//!   [`ConfigDocument`](crate::model::configdocument::ConfigDocument), the
//!   outstanding prompts and the progress displays. It never touches the tag
//!   and never blocks on it, so the view stays live during a download.
//! - The **tag thread** (`tagthread`) owns the [`tagcore::Tag`]. It polls the
//!   status, runs the engine's commands in order, and runs downloads. Its
//!   reports come back as `Msg::Tag` on the engine thread's channel.
//!
//! Everything that reaches the engine thread arrives as a `Msg` on one
//! channel, so every state change happens there and needs no lock. After
//! each message, or the 1 s tick that notices a silent tag, the loop
//! rebuilds the view state (`view.rs`) and sends it only if it differs from
//! the last one sent.
//!
//! # Logging
//!
//! [`UiLogger`] forwards the `log` crate's records -- the engine's own and
//! tagcore's -- to the sink as [`Event::Log`], filtered to the level the UI
//! chose.

pub mod download;
mod monitor;
mod tagthread;
mod view;

use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::{Duration, Instant};

use serde_json::{Value, json};

use monitor::Monitor;
use tagthread::Report;

/// Where the engine keeps its files, and what it tells Help.
#[derive(Debug, Clone)]
pub struct EngineConfig {
    /// The settings file (qtmonitor's QSettings): the recent configuration
    /// files.
    pub settings_path: PathBuf,
    /// The user's home folder, where the save dialogs start.
    pub home_dir: PathBuf,
    /// The user manual page for Help > User Guide, if one is installed.
    pub user_guide_url: Option<String>,
    /// Attach when the engine starts, as qtmonitor does unless told not to.
    pub auto_attach: bool,
}

/// What the engine tells the UI, through the sink given to [`spawn`].
///
/// The sink is called on the engine thread for [`Event::State`] and
/// [`Event::Exit`], and on whatever thread logged for [`Event::Log`] (the tag
/// thread's tagcore messages arrive from it), so it must be cheap and must
/// not call [`Engine::query`] and wait.
#[derive(Debug, Clone)]
pub enum Event {
    /// The whole view state (the contract's `state` event), sent only when it
    /// differs from the last one sent.
    State(Value),
    /// One log line at or above the level the UI set.
    Log {
        /// The level's name: `TRACE`, `DEBUG`, `INFO`, `WARN` or `ERROR`.
        level: &'static str,
        /// The message, without the level.
        text: String,
    },
    /// Quit was confirmed (any unsaved configuration saved or discarded):
    /// the shell should close the window and exit.
    Exit,
}

/// The UI's side of the engine: a cheap, cloneable sender of messages.
///
/// Every method only queues a message for the engine thread. Commands are
/// fire-and-forget, so a command's effect is seen only in a later
/// [`Event::State`].
///
/// # Examples
///
/// ```no_run
/// use serde_json::json;
/// use qtmon_core::engine::{self, EngineConfig, Event};
///
/// let config = EngineConfig {
///     settings_path: "/tmp/qtmon-scratch/settings.json".into(),
///     home_dir: "/tmp/qtmon-scratch".into(),
///     user_guide_url: None,
///     auto_attach: false,
/// };
/// let _ = log::set_logger(&engine::LOGGER);
/// let engine = engine::spawn(config, |event| {
///     if let Event::Log { level, text } = event {
///         eprintln!("{level} {text}");
///     }
/// });
/// engine.command("ready", json!({}));
/// println!("{}", engine.query("about", json!({}))["title"]);
/// ```
#[derive(Clone)]
pub struct Engine {
    /// The engine thread's message channel.
    tx: Sender<Msg>,
}

/// Messages to the engine thread, applied in the order they arrive.
#[derive(Debug)]
pub(crate) enum Msg {
    /// A UI command: its name and its JSON arguments.
    Command(String, Value),
    /// A UI query: its name, its JSON arguments and the channel for its
    /// answer.
    Query(String, Value, Sender<Value>),
    /// A report from the tag thread.
    Tag(Report),
}

impl Engine {
    /// Sends a command; see the contract's command table.
    ///
    /// Returns at once. An unknown command is logged as a warning by the
    /// engine; a command sent after the engine thread has gone is dropped.
    pub fn command(&self, cmd: &str, args: Value) {
        let _ = self.tx.send(Msg::Command(cmd.to_owned(), args));
    }

    /// Asks a query and waits for its answer; see the contract's query table.
    ///
    /// The engine thread never waits on the tag, so the answer comes after at
    /// most the messages already queued. Returns [`Value::Null`] if the
    /// engine thread has gone or does not answer within 10 s, and
    /// `{ "error": ... }` for an unknown query name.
    pub fn query(&self, name: &str, args: Value) -> Value {
        let (reply_tx, reply_rx) = mpsc::channel();
        if self
            .tx
            .send(Msg::Query(name.to_owned(), args, reply_tx))
            .is_err()
        {
            return Value::Null;
        }
        reply_rx
            .recv_timeout(Duration::from_secs(10))
            .unwrap_or(Value::Null)
    }
}

/// The event sink, shared between the engine thread and the logger.
type Sink = Arc<dyn Fn(Event) + Send + Sync>;

/// The sink [`UiLogger`] forwards to, set by [`spawn`]. `None` until the
/// engine starts, so records logged before then are dropped.
static SINK: OnceLock<Mutex<Option<Sink>>> = OnceLock::new();
/// The UI's log level, as `log::Level as u8` (1 ERROR .. 5 TRACE). Starts at
/// 3 (INFO).
static LOG_LEVEL: AtomicU8 = AtomicU8::new(3);

/// A `log` logger that forwards records to the UI's Error Log tab, as
/// qtmonitor's message handler did.
///
/// Install it once, before [`spawn`], with `log::set_logger(&LOGGER)`. Until
/// [`spawn`] sets the sink it drops everything.
pub struct UiLogger;

/// The logger to install with `log::set_logger`.
pub static LOGGER: UiLogger = UiLogger;

impl log::Log for UiLogger {
    /// Passes records at or above the UI's level.
    fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
        metadata.level() as u8 <= LOG_LEVEL.load(Ordering::Relaxed)
    }

    /// Sends one enabled record to the sink as an [`Event::Log`], with the
    /// record's message only.
    fn log(&self, record: &log::Record<'_>) {
        if !self.enabled(record.metadata()) {
            return;
        }
        let level = match record.level() {
            log::Level::Error => "ERROR",
            log::Level::Warn => "WARN",
            log::Level::Info => "INFO",
            log::Level::Debug => "DEBUG",
            log::Level::Trace => "TRACE",
        };
        if let Some(sink) = SINK.get().and_then(|s| s.lock().ok()?.clone()) {
            sink(Event::Log {
                level,
                text: record.args().to_string(),
            });
        }
    }

    /// Does nothing: nothing is buffered.
    fn flush(&self) {}
}

/// Sets the log level from its name (`TRACE`..`ERROR`); any other name
/// means `INFO`. Sets the `log` crate's global maximum too, so records below
/// the level are not even formatted.
pub(crate) fn set_log_level(name: &str) {
    let level = match name {
        "TRACE" => log::LevelFilter::Trace,
        "DEBUG" => log::LevelFilter::Debug,
        "WARN" => log::LevelFilter::Warn,
        "ERROR" => log::LevelFilter::Error,
        _ => log::LevelFilter::Info,
    };
    LOG_LEVEL.store(level as u8, Ordering::Relaxed);
    log::set_max_level(level);
}

/// Starts the engine and tag threads and returns the engine's handle.
/// `sink` receives every [`Event`].
///
/// Nothing happens until the page sends `ready`; then, with
/// [`EngineConfig::auto_attach`], the engine attaches, as qtmonitor does
/// when its window opens.
///
/// # Panics
///
/// Panics if the operating system cannot start a thread.
pub fn spawn(config: EngineConfig, sink: impl Fn(Event) + Send + Sync + 'static) -> Engine {
    let sink: Sink = Arc::new(sink);
    *SINK.get_or_init(|| Mutex::new(None)).lock().unwrap() = Some(sink.clone());
    set_log_level("INFO");
    let (tx, rx) = mpsc::channel();
    let (tag_tx, tag_rx) = mpsc::channel();
    let cancel = Arc::new(AtomicBool::new(false));
    {
        let (tx, cancel) = (tx.clone(), cancel.clone());
        std::thread::Builder::new()
            .name("qtmon-tag".into())
            .spawn(move || tagthread::run(tag_rx, tx, cancel))
            .expect("tag thread");
    }
    let engine = Engine { tx };
    std::thread::Builder::new()
        .name("qtmon-engine".into())
        .spawn(move || run(Monitor::new(config, tag_tx, cancel), sink, rx))
        .expect("engine thread");
    engine
}

/// The engine loop: apply messages, run the 1 s tick, publish the view state
/// when anything changed, and pass on a confirmed quit.
///
/// In practice the loop never ends: the tag thread holds a sender of its
/// own, so the threads live until the process exits. A confirmed Quit
/// detaches the tag first, and only then sends [`Event::Exit`].
fn run(mut monitor: Monitor, sink: Sink, rx: Receiver<Msg>) {
    let mut last_state = Value::Null;
    loop {
        let wait = monitor
            .next_deadline()
            .map(|d| d.saturating_duration_since(Instant::now()))
            .unwrap_or(Duration::from_secs(3600));
        match rx.recv_timeout(wait) {
            Ok(Msg::Command(cmd, args)) => monitor.command(&cmd, &args),
            Ok(Msg::Query(name, args, reply)) => {
                let _ = reply.send(monitor.query(&name, &args));
            }
            Ok(Msg::Tag(report)) => monitor.report(report),
            Err(RecvTimeoutError::Timeout) => {}
            Err(RecvTimeoutError::Disconnected) => break,
        }
        monitor.fire_timers(Instant::now());
        if monitor.take_republish() {
            last_state = Value::Null;
        }
        let state = monitor.view_state();
        if state != last_state {
            sink(Event::State(state.clone()));
            last_state = state;
        }
        if monitor.take_exit() {
            sink(Event::Exit);
        }
    }
}

/// `{ "error": text }`, for a query that cannot be answered.
pub(crate) fn query_error(text: &str) -> Value {
    json!({ "error": text })
}
