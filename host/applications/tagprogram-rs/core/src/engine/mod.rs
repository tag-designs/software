//! The engine: qtprogram's `Station` and `ProgramSession`, with no UI.
//!
//! [`spawn`] starts the engine on its own thread and returns an [`Engine`]
//! handle. The UI talks to it only through [`Engine::command`] and
//! [`Engine::query`], and hears from it only through the [`Event`] sink: the
//! whole view state after every change, and log lines. The protocol is
//! `design/ui-engine-contract.md`; the internals are `design/engine.md`.
//!
//! # Threading
//!
//! The engine thread (`run`) owns all state, in one `Station`; nothing
//! else ever holds a reference to it. Everything that reaches the station
//! arrives as a `Msg` on one channel:
//!
//! - `Msg::Command` and `Msg::Query` from the UI, through [`Engine`];
//! - `Msg::Done` closures from worker threads (and, once, from the station
//!   itself), applied to the station on the engine thread, so that every
//!   state change happens there and needs no lock.
//!
//! Timers -- the base poll (while no base is on USB), the status poll (while
//! a tag is attached), the attach retry and the session's 1 s tick -- are
//! deadlines kept in the station; the loop waits on the channel until the
//! nearest one and fires those that are due. Work that takes seconds --
//! listing and downloading releases, the SWD connect check, programming,
//! reading flash back -- runs on a worker thread started with
//! `Station::spawn_work`, so the view never freezes. Work on the tag's
//! monitor (attach, status, stop, erase, self-tests, SetRtc) is quick and
//! runs on the engine thread, as it did on qtprogram's GUI thread.
//!
//! After each message or timeout the loop rebuilds the view state
//! (`view.rs`) and sends it only if it differs from the last one sent.
//!
//! # Modules
//!
//! - `station`: the eight checks, base selection, attach and polling, the
//!   firmware choice, the record file and prompts (`station.cpp`).
//! - `session`: the program and retest step machine
//!   (`programsession.cpp`, and `Station::program`/`retest`/`writeRecord`).
//! - `view`: the view-state JSON, with `MainWindow`'s display rules
//!   (`mainwindow.cpp`).
//!
//! # Logging
//!
//! [`UiLogger`] forwards the `log` crate's records -- the engine's own and
//! those of `tagcore` and `tagprobe` (probe-rs progress) -- to the sink as
//! [`Event::Log`], filtered to the level the UI chose.

mod session;
mod station;
mod view;

use std::path::PathBuf;
use std::sync::atomic::{AtomicU8, Ordering};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::{Duration, Instant};

use serde_json::{Value, json};

use station::Station;

/// Where the engine keeps its files, and what it tells Help.
///
/// The shell fills these from the platform's directories; the examples point
/// them all at a scratch directory so a run leaves the user's settings alone.
#[derive(Debug, Clone)]
pub struct EngineConfig {
    /// Cache of downloaded releases (qtprogram's `AppDataLocation/firmware`).
    pub cache_dir: PathBuf,
    /// The settings file (qtprogram's QSettings).
    pub settings_path: PathBuf,
    /// The user's Documents folder, for the default record path.
    pub documents_dir: PathBuf,
    /// The user manual page for Help > User Guide, if one is installed.
    pub user_guide_url: Option<String>,
}

/// What the engine tells the UI, through the sink given to [`spawn`].
///
/// The sink is called on the engine thread for [`Event::State`] and on
/// whatever thread logged for [`Event::Log`] (a worker's probe-rs progress
/// arrives from the worker), so it must be cheap and must not call back into
/// the engine and wait: [`Engine::query`] from inside the sink would deadlock
/// the engine thread until the query's 10 s timeout.
#[derive(Debug, Clone)]
pub enum Event {
    /// The whole view state (the contract's `state` event), sent only when it
    /// differs from the last one sent.
    State(Value),
    /// One log line at or above the level the UI set (`set_log_level`).
    Log {
        /// The level's name: `TRACE`, `DEBUG`, `INFO`, `WARN` or `ERROR`.
        level: &'static str,
        /// The message, without the level.
        text: String,
    },
}

/// The UI's side of the engine: a cheap, cloneable sender of messages.
///
/// Every method only queues a message for the engine thread; none touches
/// the station directly. Commands are fire-and-forget, so a command's effect
/// is seen only in a later [`Event::State`].
///
/// # Examples
///
/// ```no_run
/// use serde_json::json;
/// use tagprogram_core::engine::{self, EngineConfig, Event};
///
/// let dir = std::path::PathBuf::from("/tmp/tagprogram-scratch");
/// let config = EngineConfig {
///     cache_dir: dir.join("firmware"),
///     settings_path: dir.join("settings.json"),
///     documents_dir: dir.join("Documents"),
///     user_guide_url: None,
/// };
/// let _ = log::set_logger(&engine::LOGGER);
/// let engine = engine::spawn(config, |event| {
///     if let Event::Log { level, text } = event {
///         eprintln!("{level} {text}");
///     }
/// });
/// engine.command("start", json!({}));
/// let about = engine.query("about", json!({}));
/// println!("{}", about["title"]);
/// ```
#[derive(Clone)]
pub struct Engine {
    /// The engine thread's message channel.
    tx: Sender<Msg>,
}

/// Messages to the engine thread, applied in the order they arrive.
pub(crate) enum Msg {
    /// A UI command: its name and its JSON arguments (see
    /// [`Station::command`]).
    Command(String, Value),
    /// A UI query: its name, its JSON arguments and the channel for its
    /// answer (see [`Station::query`]).
    Query(String, Value, Sender<Value>),
    /// A worker finished, or reports progress; its closure is applied to the
    /// station on the engine thread. The station also posts one to itself to
    /// defer opening the record until after the first view is published
    /// (`Station::begin_checks`).
    Done(Box<dyn FnOnce(&mut Station) + Send>),
}

impl Engine {
    /// Sends a command; see the contract's command table.
    ///
    /// Returns at once. An unknown command is logged as a warning by the engine;
    /// a command sent after the engine thread has gone is dropped silently.
    pub fn command(&self, cmd: &str, args: Value) {
        let _ = self.tx.send(Msg::Command(cmd.to_owned(), args));
    }

    /// Asks a query and waits for its answer; see the contract's query table.
    ///
    /// Blocks the calling thread until the engine thread answers, which it does
    /// between other messages, so the wait includes any command or `Done` closure
    /// ahead in the queue -- and those can include synchronous monitor traffic to
    /// the tag.
    ///
    /// Returns [`Value::Null`] if the engine thread has gone or does not answer
    /// within 10 s, and `{ "error": ... }` for an unknown query name.
    ///
    /// # Panics
    ///
    /// Never itself; but calling it on the engine thread (from the event sink)
    /// blocks that thread for the full timeout.
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
/// engine starts, so records logged before then are dropped. A second
/// [`spawn`] in the same process replaces it.
static SINK: OnceLock<Mutex<Option<Sink>>> = OnceLock::new();
/// The UI's log level, as `log::Level as u8` (1 ERROR .. 5 TRACE).
///
/// Read by [`UiLogger`]'s `enabled` on every record, from any thread; written
/// only by [`set_log_level`]. Starts at 3 (INFO).
static LOG_LEVEL: AtomicU8 = AtomicU8::new(3);

/// A `log` logger that forwards records to the UI's log pane, so tagcore's
/// and tagprobe's messages appear there as qtprogram's did.
///
/// Install it once, before [`spawn`], with `log::set_logger(&LOGGER)`. Until
/// [`spawn`] sets the sink it drops everything. It has no buffer: each record
/// is passed to the sink on the thread that logged it.
pub struct UiLogger;

/// The logger to install with `log::set_logger`.
pub static LOGGER: UiLogger = UiLogger;

impl log::Log for UiLogger {
    /// Passes records at or above the UI's level (`LOG_LEVEL`).
    fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
        metadata.level() as u8 <= LOG_LEVEL.load(Ordering::Relaxed)
    }

    /// Sends one enabled record to the sink as an [`Event::Log`], with the
    /// record's message only (no target or module path).
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

/// Sets the log pane's level from its name (`TRACE`..`ERROR`).
///
/// Any other name means `INFO`. Sets both [`LOG_LEVEL`] and the `log`
/// crate's global maximum, so records below the level are not even
/// formatted. Called from the engine thread (the `set_log_level` command)
/// and once by [`spawn`].
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

/// Starts the engine thread and returns its handle. `sink` receives every
/// [`Event`].
///
/// Sets the logger's sink and resets the log level to INFO, then builds the
/// `Station` on the new thread, which loads the settings file and the
/// cached release list. Nothing else happens until the UI sends `start`:
/// the first-run prompt, the checks and the catalog refresh all wait for it.
///
/// # Panics
///
/// Panics if the operating system cannot start a thread.
pub fn spawn(config: EngineConfig, sink: impl Fn(Event) + Send + Sync + 'static) -> Engine {
    let sink: Sink = Arc::new(sink);
    *SINK.get_or_init(|| Mutex::new(None)).lock().unwrap() = Some(sink.clone());
    set_log_level("INFO");
    let (tx, rx) = mpsc::channel();
    let engine = Engine { tx: tx.clone() };
    std::thread::Builder::new()
        .name("tagprogram-engine".into())
        .spawn(move || run(config, sink, tx, rx))
        .expect("engine thread");
    engine
}

/// The engine loop: apply messages, fire due timers, publish the view state
/// when anything changed.
///
/// Waits on the channel until the next timer deadline (or an hour when none
/// is armed), so an idle engine costs nothing. Every pass -- message or
/// timeout -- fires due timers and rebuilds the view state; the state is sent
/// only when it differs from the last one sent.
///
/// The loop ends when the channel disconnects, which in practice it never
/// does: the station holds a sender of its own (for `spawn_work`), so the
/// thread lives until the process exits.
fn run(config: EngineConfig, sink: Sink, tx: Sender<Msg>, rx: Receiver<Msg>) {
    let mut station = Station::new(config, tx);
    let mut last_state = Value::Null;
    loop {
        let wait = station
            .next_deadline()
            .map(|d| d.saturating_duration_since(Instant::now()))
            .unwrap_or(Duration::from_secs(3600));
        match rx.recv_timeout(wait) {
            Ok(Msg::Command(cmd, args)) => station.command(&cmd, &args),
            Ok(Msg::Query(name, args, reply)) => {
                let _ = reply.send(station.query(&name, &args));
            }
            Ok(Msg::Done(apply)) => apply(&mut station),
            Err(RecvTimeoutError::Timeout) => {}
            Err(RecvTimeoutError::Disconnected) => break,
        }
        station.fire_timers(Instant::now());
        let state = station.view_state();
        if state != last_state {
            sink(Event::State(state.clone()));
            last_state = state;
        }
    }
}

/// `{ "error": text }`, for a query that cannot be answered.
pub(crate) fn query_error(text: &str) -> Value {
    json!({ "error": text })
}
