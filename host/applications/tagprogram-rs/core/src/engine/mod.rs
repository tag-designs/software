//! The engine: qtprogram's `Station` and `ProgramSession`, with no UI.
//!
//! [`spawn`] starts the engine on its own thread. The UI talks to it only
//! through [`Engine::command`] and [`Engine::query`], and hears from it only
//! through the [`Event`] sink: the whole view state after every change, and
//! log lines. The protocol is `design/ui-engine-contract.md`.
//!
//! The thread owns all state. Timers (base poll, status poll, session tick,
//! attach retry) are deadlines checked by its loop. Work that takes seconds
//! -- downloading a release, programming, reading flash back -- runs on a
//! worker thread and reports back as a message, so the view never freezes;
//! work on the tag's monitor (attach, status, stop, erase, tests) is quick
//! and runs on the engine thread, as it did on qtprogram's.

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

/// Where the engine keeps its files.
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

/// What the engine tells the UI.
#[derive(Debug, Clone)]
pub enum Event {
    /// The whole view state (the contract's `state` event).
    State(Value),
    /// One log line at or above the level the UI set.
    Log { level: &'static str, text: String },
}

/// The UI's side of the engine.
#[derive(Clone)]
pub struct Engine {
    tx: Sender<Msg>,
}

/// Messages to the engine thread.
pub(crate) enum Msg {
    Command(String, Value),
    Query(String, Value, Sender<Value>),
    /// A worker finished; its closure is applied on the engine thread.
    Done(Box<dyn FnOnce(&mut Station) + Send>),
}

impl Engine {
    /// Sends a command; see the contract's command table.
    pub fn command(&self, cmd: &str, args: Value) {
        let _ = self.tx.send(Msg::Command(cmd.to_owned(), args));
    }

    /// Asks a query and waits for its answer; see the contract's query table.
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

/// The event sink, shared with the logger.
type Sink = Arc<dyn Fn(Event) + Send + Sync>;

static SINK: OnceLock<Mutex<Option<Sink>>> = OnceLock::new();
/// The UI's log level, as `log::Level as u8` (1 ERROR .. 5 TRACE).
static LOG_LEVEL: AtomicU8 = AtomicU8::new(3);

/// A `log` logger that forwards records to the UI's log pane, so tagcore's
/// and tagprobe's messages appear there as qtprogram's did. Install it once,
/// before [`spawn`], with `log::set_logger(&LOGGER)`.
pub struct UiLogger;

/// The logger to install.
pub static LOGGER: UiLogger = UiLogger;

impl log::Log for UiLogger {
    fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
        metadata.level() as u8 <= LOG_LEVEL.load(Ordering::Relaxed)
    }

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

    fn flush(&self) {}
}

/// Sets the log pane's level from its name (TRACE..ERROR).
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

/// Starts the engine thread. `sink` receives every [`Event`].
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
