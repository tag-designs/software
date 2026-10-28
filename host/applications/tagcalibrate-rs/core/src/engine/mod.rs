//! The engine: qtcalibrate's `MainWindow` behaviour, with no UI.
//!
//! [`spawn`] starts the engine and returns an [`Engine`] handle. The UI talks
//! to it only through [`Engine::command`] and [`Engine::query`], and hears
//! from it only through the [`Event`] sink: the whole view state after every
//! change, log lines, and the request to exit.
//!
//! # Threading
//!
//! Two threads, as in Tag Monitor (`tagmonitor-rs/core/src/engine`):
//!
//! - The **engine thread** (`run`) owns all state, in one `Calibrator`: the
//!   calibration session, the sample capture, the display settings, the
//!   outstanding prompt and the timers. It never touches the tag.
//! - The **tag thread** (`tagthread`) owns the [`tagcore::Tag`]. It runs the
//!   engine's commands in order and, while streaming, reads the calibration
//!   log every 100 ms, as qtcalibrate's sample timer does. Its reports come
//!   back as `Msg::Tag` on the engine thread's channel.
//!
//! A replay ([`EngineConfig::replay`]) needs no tag: the engine feeds a
//! capture's samples itself, one per 100 ms tick, as qtcalibrate's
//! `--replay-capture` does.
//!
//! # Logging
//!
//! [`UiLogger`] forwards the `log` crate's records -- the engine's own and
//! tagcore's -- to the sink as [`Event::Log`], filtered to the level the UI
//! chose. Each quality update logs the four `DEBUG` lines qtcalibrate logs
//! (`magquality:`, `residual:`, `gravity:`, `retention:`), in its format, so
//! a replay's log can be compared with qtcalibrate's line for line.

mod calibrator;
mod capture;
mod tagthread;
mod view;

use std::path::PathBuf;
use std::sync::atomic::{AtomicU8, Ordering};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::{Duration, Instant};

use serde_json::{Value, json};

use calibrator::Calibrator;
use tagthread::Report;

/// Where the engine keeps its files, what it tells Help, and how it starts.
#[derive(Debug, Clone)]
pub struct EngineConfig {
    /// The user's home folder, where the save dialogs start.
    pub home_dir: PathBuf,
    /// The user manual page for Help > User Guide, if one is installed.
    pub user_guide_url: Option<String>,
    /// Attach when the engine starts, as qtcalibrate does.
    pub auto_attach: bool,
    /// Replay this capture file (qtcalibrate's sample-capture JSON) instead
    /// of attaching to a tag; the window then looks attached to it.
    pub replay: Option<PathBuf>,
    /// The time between replayed samples: [`REPLAY_PERIOD`], qtcalibrate's
    /// sample tick, in the app. Results do not depend on it -- the quality
    /// update counts samples in a replay -- so a test may shorten it.
    pub replay_period: Duration,
}

/// The time between replayed samples in the app: qtcalibrate's 100 ms tick.
pub const REPLAY_PERIOD: Duration = Duration::from_millis(100);

/// What the engine tells the UI, through the sink given to [`spawn`].
///
/// The sink is called on the engine thread for [`Event::State`] and
/// [`Event::Exit`], and on whatever thread logged for [`Event::Log`], so it
/// must be cheap and must not call [`Engine::query`] and wait.
#[derive(Debug, Clone)]
pub enum Event {
    /// The whole view state, sent only when it differs from the last one.
    State(Value),
    /// One log line at or above the level the UI set.
    Log {
        /// The level's name: `TRACE`, `DEBUG`, `INFO`, `WARN` or `ERROR`.
        level: &'static str,
        /// The message, without the level.
        text: String,
    },
    /// Quit was confirmed and the tag detached: the shell should exit.
    Exit,
}

/// The UI's side of the engine: a cheap, cloneable sender of messages.
///
/// Commands are fire-and-forget; a command's effect is seen only in a later
/// [`Event::State`].
///
/// # Examples
///
/// ```no_run
/// use serde_json::json;
/// use tagcalibrate_core::engine::{self, EngineConfig, Event};
///
/// let config = EngineConfig {
///     home_dir: "/tmp".into(),
///     user_guide_url: None,
///     auto_attach: false,
///     replay: None,
///     replay_period: engine::REPLAY_PERIOD,
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
    /// Sends a command. Returns at once; an unknown command is logged as a
    /// warning, and a command sent after the engine has gone is dropped.
    pub fn command(&self, cmd: &str, args: Value) {
        let _ = self.tx.send(Msg::Command(cmd.to_owned(), args));
    }

    /// Asks a query and waits for its answer: [`Value::Null`] if the engine
    /// thread has gone or does not answer within 10 s, and
    /// `{ "error": ... }` for an unknown query.
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

/// The sink [`UiLogger`] forwards to, set by [`spawn`].
static SINK: OnceLock<Mutex<Option<Sink>>> = OnceLock::new();
/// The UI's log level, as `log::Level as u8` (1 ERROR .. 5 TRACE).
static LOG_LEVEL: AtomicU8 = AtomicU8::new(3);

/// A `log` logger that forwards records to the UI's Log tab, as qtcalibrate's
/// message handler did. Install it once, before [`spawn`].
pub struct UiLogger;

/// The logger to install with `log::set_logger`.
pub static LOGGER: UiLogger = UiLogger;

impl log::Log for UiLogger {
    /// Passes records at or above the UI's level.
    fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
        metadata.level() as u8 <= LOG_LEVEL.load(Ordering::Relaxed)
    }

    /// Sends one enabled record to the sink as an [`Event::Log`].
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

/// Sets the log level from its name (`TRACE`..`ERROR`); any other name means
/// `INFO`.
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
///
/// Nothing happens until the page sends `ready`; then the engine sets up a
/// replay, or with [`EngineConfig::auto_attach`] attaches.
///
/// # Panics
///
/// Panics if a thread cannot be started, or the calibration library cannot
/// make a session (built from another version of its header, or out of
/// memory).
pub fn spawn(config: EngineConfig, sink: impl Fn(Event) + Send + Sync + 'static) -> Engine {
    let sink: Sink = Arc::new(sink);
    *SINK.get_or_init(|| Mutex::new(None)).lock().unwrap() = Some(sink.clone());
    set_log_level("INFO");
    let (tx, rx) = mpsc::channel();
    let (tag_tx, tag_rx) = mpsc::channel();
    {
        let tx = tx.clone();
        std::thread::Builder::new()
            .name("tagcalibrate-tag".into())
            .spawn(move || tagthread::run(tag_rx, tx))
            .expect("tag thread");
    }
    let calibrator = Calibrator::new(config, tag_tx).expect("calibration session");
    std::thread::Builder::new()
        .name("tagcalibrate-engine".into())
        .spawn(move || run(calibrator, sink, rx))
        .expect("engine thread");
    Engine { tx }
}

/// The engine loop: apply messages, run the timers, publish the view state
/// when it changed, and pass on a confirmed quit.
fn run(mut calibrator: Calibrator, sink: Sink, rx: Receiver<Msg>) {
    let mut last_state = Value::Null;
    loop {
        let wait = calibrator
            .next_deadline()
            .map(|d| d.saturating_duration_since(Instant::now()))
            .unwrap_or(Duration::from_secs(3600));
        match rx.recv_timeout(wait) {
            Ok(Msg::Command(cmd, args)) => calibrator.command(&cmd, &args),
            Ok(Msg::Query(name, args, reply)) => {
                let _ = reply.send(calibrator.query(&name, &args));
            }
            Ok(Msg::Tag(report)) => calibrator.report(report),
            Err(RecvTimeoutError::Timeout) => {}
            Err(RecvTimeoutError::Disconnected) => break,
        }
        calibrator.fire_timers(Instant::now());
        let state = calibrator.view_state();
        if state != last_state {
            sink(Event::State(state.clone()));
            last_state = state;
        }
        if calibrator.take_exit() {
            sink(Event::Exit);
        }
    }
}

/// `{ "error": text }`, for a query that cannot be answered.
pub(crate) fn query_error(text: &str) -> Value {
    json!({ "error": text })
}
