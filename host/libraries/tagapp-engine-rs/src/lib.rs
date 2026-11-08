//! The engine toolkit the Tauri applications' core crates share.
//!
//! Every application's engine has the same outer shape: a handle the shell
//! sends commands and queries through, one engine thread that owns all state
//! and applies messages in order, a view state sent to the page only when it
//! changed, prompts the page answers by id, a `ready` command that starts the
//! engine once and resends the whole state on a reload, and a logger that
//! forwards the `log` crate's records to the page. This crate is that shape;
//! an application supplies only its state, through [`EngineState`].
//!
//! It has no Tauri dependency, so a core crate builds and tests without a
//! window. The shell (`tagapp-shell`) wraps an [`Engine`] for the page.
//!
//! # Starting an engine
//!
//! ```
//! use std::time::Duration;
//! use serde_json::{Value, json};
//! use tagapp_engine::{EngineState, Event};
//!
//! /// A counter: `add` adds `n`, the `total` query reads it.
//! struct Counter(i64);
//!
//! impl EngineState for Counter {
//!     type Msg = ();
//!     fn command(&mut self, cmd: &str, args: &Value) {
//!         if cmd == "add" {
//!             self.0 += args["n"].as_i64().unwrap_or(0);
//!         }
//!     }
//!     fn query(&mut self, _name: &str, _args: &Value) -> Value {
//!         json!(self.0)
//!     }
//!     fn view_state(&mut self) -> Value {
//!         json!({ "total": self.0 })
//!     }
//! }
//!
//! let (engine, _poster, inbox) = tagapp_engine::channel(Duration::from_secs(10), |event| {
//!     if let Event::State(state) = event {
//!         println!("{state}");
//!     }
//! });
//! tagapp_engine::spawn("counter-engine", Counter(0), inbox);
//! engine.command("add", json!({ "n": 2 }));
//! assert_eq!(engine.query("total", json!({})), json!(2));
//! ```
//!
//! An application with a tag thread hands that thread the [`Poster`], which
//! queues the thread's reports on the engine thread's channel as
//! [`EngineState::Msg`] values.

use std::collections::VecDeque;
use std::sync::atomic::{AtomicU8, Ordering};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::{Duration, Instant};

use serde_json::Value;

/// What an engine tells the UI, through the sink given to [`channel`].
///
/// The sink is called on the engine thread for [`Event::State`] and
/// [`Event::Exit`], and on whatever thread logged for [`Event::Log`] (a tag
/// thread's tagcore messages arrive from it), so it must be cheap and must
/// not call [`Engine::query`] and wait.
#[derive(Debug, Clone)]
pub enum Event {
    /// The whole view state, sent only when it differs from the last one sent
    /// (or after a `ready`; see [`EngineState::take_republish`]).
    State(Value),
    /// One log line at or above the level the UI set (see [`set_log_level`]).
    Log {
        /// The level's name: `TRACE`, `DEBUG`, `INFO`, `WARN` or `ERROR`.
        level: &'static str,
        /// The message, without the level.
        text: String,
    },
    /// Quit was confirmed (and whatever had to happen first has happened):
    /// the shell should close the window and exit.
    Exit,
}

/// An application's engine state: everything the engine thread owns.
///
/// [`spawn`] calls these on the engine thread only, so the state needs no
/// locks. After every message, and whenever the deadline from
/// [`EngineState::next_deadline`] passes, the loop calls
/// [`EngineState::fire_timers`], then rebuilds the view state and sends it
/// if it changed, then sends [`Event::Exit`] if [`EngineState::take_exit`]
/// says so.
pub trait EngineState: Send + 'static {
    /// The application's own messages, posted through a [`Poster`]: a tag
    /// thread's reports, or `()` for an application without one.
    type Msg: Send + 'static;

    /// Applies one UI command. An unknown one should be logged as a warning.
    fn command(&mut self, cmd: &str, args: &Value);

    /// Answers one UI query; `{ "error": ... }` (see [`query_error`]) for an
    /// unknown one.
    fn query(&mut self, name: &str, args: &Value) -> Value;

    /// The whole view state the page draws from.
    fn view_state(&mut self) -> Value;

    /// Applies one of the application's own messages. The default ignores it.
    fn message(&mut self, msg: Self::Msg) {
        let _ = msg;
    }

    /// The earliest timer due, if any. The default has no timers.
    fn next_deadline(&self) -> Option<Instant> {
        None
    }

    /// Runs the timers due at `now`. Called after every message, so it must
    /// check its own deadlines. The default does nothing.
    fn fire_timers(&mut self, now: Instant) {
        let _ = now;
    }

    /// True once after a `ready`, so the loop sends the whole view state
    /// even if it has not changed: a page that reloaded has lost it. An
    /// application keeps a [`Ready`] and returns its
    /// [`Ready::take_republish`]. The default never resends.
    fn take_republish(&mut self) -> bool {
        false
    }

    /// True once when the shell should exit. The default never exits, for an
    /// application whose shell closes without asking its engine.
    fn take_exit(&mut self) -> bool {
        false
    }
}

/// A UI request to the engine thread.
#[derive(Debug)]
enum Request {
    /// A command: its name and its JSON arguments.
    Command(String, Value),
    /// A query: its name, its JSON arguments and the channel for its answer.
    Query(String, Value, Sender<Value>),
}

/// A message to the engine thread, applied in the order it arrives.
#[derive(Debug)]
enum Msg<M> {
    /// From the UI, through the [`Engine`].
    Ui(Request),
    /// From the application, through a [`Poster`].
    App(M),
}

/// Sends a request to the engine thread; false once the thread has gone.
type SendRequest = Arc<dyn Fn(Request) -> bool + Send + Sync>;

/// The UI's side of an engine: a cheap, cloneable sender of messages.
///
/// Commands are fire-and-forget; a command's effect is seen only in a later
/// [`Event::State`]. The application's message type is hidden here, so every
/// engine's handle is the same type.
#[derive(Clone)]
pub struct Engine {
    /// Queues a request on the engine thread's channel.
    send: SendRequest,
    /// How long [`Engine::query`] waits for its answer.
    query_timeout: Duration,
}

impl Engine {
    /// Sends a command. Returns at once; an unknown command is the
    /// application's to log, and a command sent after the engine thread has
    /// gone is dropped.
    pub fn command(&self, cmd: &str, args: Value) {
        (self.send)(Request::Command(cmd.to_owned(), args));
    }

    /// Asks a query and waits for its answer: [`Value::Null`] if the engine
    /// thread has gone or does not answer within the timeout given to
    /// [`channel`], and whatever the application answers otherwise
    /// (`{ "error": ... }` for an unknown query).
    ///
    /// # Warning
    ///
    /// Blocks the calling thread. The shell calls it off the UI thread; an
    /// event sink must never call it.
    pub fn query(&self, name: &str, args: Value) -> Value {
        let (reply_tx, reply_rx) = mpsc::channel();
        if !(self.send)(Request::Query(name.to_owned(), args, reply_tx)) {
            return Value::Null;
        }
        reply_rx
            .recv_timeout(self.query_timeout)
            .unwrap_or(Value::Null)
    }
}

/// The application's side of the engine channel: queues an
/// [`EngineState::Msg`] for the engine thread, from any thread.
pub struct Poster<M> {
    /// The engine thread's channel.
    tx: Sender<Msg<M>>,
}

// Derived Clone would require `M: Clone`; a sender clones whatever it sends.
impl<M> Clone for Poster<M> {
    /// Another poster on the same channel.
    fn clone(&self) -> Self {
        Poster {
            tx: self.tx.clone(),
        }
    }
}

impl<M> Poster<M> {
    /// Queues `msg`. Returns false, and drops it, once the engine thread has
    /// gone.
    pub fn post(&self, msg: M) -> bool {
        self.tx.send(Msg::App(msg)).is_ok()
    }
}

/// The engine thread's end of the channel, with the sink it sends events to;
/// given to [`spawn`].
pub struct Inbox<M> {
    /// Every message for the engine thread.
    rx: Receiver<Msg<M>>,
    /// Where view states and the exit request go.
    sink: Sink,
}

/// The event sink, shared between the engine thread and the logger.
type Sink = Arc<dyn Fn(Event) + Send + Sync>;

/// Makes an engine's channel: the UI's [`Engine`], the application's
/// [`Poster`], and the [`Inbox`] to [`spawn`] the engine thread on.
///
/// `sink` receives every [`Event`]. It is also installed as the
/// [`UiLogger`]'s destination at once, so a tag thread started before
/// [`spawn`] already logs to the page, and the log level is reset to `INFO`.
/// `query_timeout` is how long [`Engine::query`] waits: 10 s is right unless
/// a command ahead of a query can take longer (Tag View's large logs open in
/// up to 30 s).
pub fn channel<M: Send + 'static>(
    query_timeout: Duration,
    sink: impl Fn(Event) + Send + Sync + 'static,
) -> (Engine, Poster<M>, Inbox<M>) {
    let sink: Sink = Arc::new(sink);
    *LOG_SINK.get_or_init(|| Mutex::new(None)).lock().unwrap() = Some(sink.clone());
    set_log_level("INFO");
    let (tx, rx) = mpsc::channel::<Msg<M>>();
    let ui_tx = tx.clone();
    let engine = Engine {
        send: Arc::new(move |request| ui_tx.send(Msg::Ui(request)).is_ok()),
        query_timeout,
    };
    (engine, Poster { tx }, Inbox { rx, sink })
}

/// Starts the engine thread, named `name`, on `state`: the loop described
/// at [`EngineState`]. It runs until every [`Engine`] and [`Poster`] has been
/// dropped -- in an application, until the process exits.
///
/// # Panics
///
/// Panics if the operating system cannot start a thread.
pub fn spawn<S: EngineState>(name: &str, state: S, inbox: Inbox<S::Msg>) {
    std::thread::Builder::new()
        .name(name.into())
        .spawn(move || run_loop(state, inbox))
        .expect("engine thread");
}

/// The engine loop: apply messages, run the timers, publish the view state
/// when it changed (or a `ready` asked for it), and pass on an exit.
fn run_loop<S: EngineState>(mut state: S, inbox: Inbox<S::Msg>) {
    let mut last_state = Value::Null;
    loop {
        let wait = state
            .next_deadline()
            .map(|d| d.saturating_duration_since(Instant::now()))
            .unwrap_or(Duration::from_secs(3600));
        match inbox.rx.recv_timeout(wait) {
            Ok(Msg::Ui(Request::Command(cmd, args))) => state.command(&cmd, &args),
            Ok(Msg::Ui(Request::Query(name, args, reply))) => {
                let _ = reply.send(state.query(&name, &args));
            }
            Ok(Msg::App(msg)) => state.message(msg),
            Err(RecvTimeoutError::Timeout) => {}
            Err(RecvTimeoutError::Disconnected) => break,
        }
        state.fire_timers(Instant::now());
        if state.take_republish() {
            last_state = Value::Null;
        }
        let view = state.view_state();
        if view != last_state {
            (inbox.sink)(Event::State(view.clone()));
            last_state = view;
        }
        if state.take_exit() {
            (inbox.sink)(Event::Exit);
        }
    }
}

/// `{ "error": text }`, for a query that cannot be answered.
pub fn query_error(text: &str) -> Value {
    serde_json::json!({ "error": text })
}

// ---- ready ------------------------------------------------------------------

/// The `ready` command's guard: the page sends `ready` when it has loaded,
/// and again after every reload.
///
/// The first `ready` starts the engine (attach, open the startup file); every
/// one asks the loop to resend the whole view state. Starting twice would
/// attach twice or reopen a file the user has since closed.
#[derive(Debug, Default)]
pub struct Ready {
    /// A `ready` has been received.
    started: bool,
    /// A `ready` arrived since the loop last asked.
    republish: bool,
}

impl Ready {
    /// Records a `ready`. True the first time only: the caller starts the
    /// engine then.
    pub fn ready(&mut self) -> bool {
        self.republish = true;
        !std::mem::replace(&mut self.started, true)
    }

    /// Whether a `ready` has been received.
    pub fn started(&self) -> bool {
        self.started
    }

    /// True once after each `ready`; return it from
    /// [`EngineState::take_republish`].
    pub fn take_republish(&mut self) -> bool {
        std::mem::take(&mut self.republish)
    }
}

// ---- prompts ----------------------------------------------------------------

/// A prompt and the id the page answers it by.
#[derive(Debug)]
pub struct Prompt<K> {
    /// Increases with each prompt asked, from 1.
    pub id: u64,
    /// What it asks: the application's prompt kind.
    pub kind: K,
}

/// The page's answer to a prompt: `{id, button, index?, path?}`.
#[derive(Debug)]
pub struct Answer<K> {
    /// The prompt answered.
    pub kind: K,
    /// The button pressed: `ok`, `cancel` (also when none was given), or an
    /// application's own (`save`, `discard`, `quit_anyway`, ...).
    pub button: String,
    /// The item chosen from a list.
    pub index: Option<usize>,
    /// The file chosen; `None` for an empty path.
    pub path: Option<String>,
}

impl<K> Answer<K> {
    /// Whether the button was `ok`.
    pub fn ok(&self) -> bool {
        self.button == "ok"
    }
}

/// The prompts waiting for the page, oldest first; the page shows the front
/// one.
///
/// A queue rather than one slot: a prompt raised while another is up waits
/// its turn instead of replacing it, so no message is lost. Each prompt gets
/// a fresh id, and an answer is taken only for the front prompt's id, so an
/// answer to a prompt the page has stopped showing cannot be misapplied.
#[derive(Debug)]
pub struct Prompts<K> {
    /// The waiting prompts; the front one is on screen.
    queue: VecDeque<Prompt<K>>,
    /// The id the next prompt gets.
    next_id: u64,
}

impl<K> Default for Prompts<K> {
    /// No prompts; the first gets id 1.
    fn default() -> Self {
        Prompts {
            queue: VecDeque::new(),
            next_id: 1,
        }
    }
}

impl<K> Prompts<K> {
    /// Queues a prompt and returns its id.
    pub fn ask(&mut self, kind: K) -> u64 {
        let id = self.next_id;
        self.next_id += 1;
        self.queue.push_back(Prompt { id, kind });
        id
    }

    /// The prompt on screen, for the view state.
    pub fn front(&self) -> Option<&Prompt<K>> {
        self.queue.front()
    }

    /// Whether no prompt is waiting.
    pub fn is_empty(&self) -> bool {
        self.queue.is_empty()
    }

    /// Takes the page's answer, `args` being `{id, button, index?, path?}`.
    ///
    /// Returns the answered prompt, removed from the queue, with the parsed
    /// answer; `None`, leaving the queue alone, when `id` is missing or is
    /// not the front prompt's (a stale answer).
    pub fn answer(&mut self, args: &Value) -> Option<Answer<K>> {
        let id = args.get("id").and_then(Value::as_u64)?;
        if self.queue.front().map(|p| p.id) != Some(id) {
            log::debug!("Ignoring a stale answer");
            return None;
        }
        let prompt = self.queue.pop_front()?;
        Some(Answer {
            kind: prompt.kind,
            button: args
                .get("button")
                .and_then(Value::as_str)
                .unwrap_or("cancel")
                .to_owned(),
            index: args
                .get("index")
                .and_then(Value::as_u64)
                .map(|i| i as usize),
            path: args
                .get("path")
                .and_then(Value::as_str)
                .filter(|p| !p.is_empty())
                .map(str::to_owned),
        })
    }

    /// Removes every waiting prompt for which `drop` is true (for example,
    /// the prompts that only make sense while a tag is attached).
    pub fn remove_if(&mut self, mut drop: impl FnMut(&K) -> bool) {
        self.queue.retain(|p| !drop(&p.kind));
    }
}

// ---- logging ----------------------------------------------------------------

/// The sink [`UiLogger`] forwards to, set by [`channel`]. `None` until an
/// engine is made, so records logged before then are dropped.
static LOG_SINK: OnceLock<Mutex<Option<Sink>>> = OnceLock::new();
/// The UI's log level, as `log::Level as u8` (1 ERROR .. 5 TRACE). Starts at
/// 3 (INFO).
static LOG_LEVEL: AtomicU8 = AtomicU8::new(3);

/// A `log` logger that forwards records -- the engine's own, and those of
/// the libraries it calls -- to the page's log pane as [`Event::Log`],
/// filtered to the level the UI chose.
///
/// Install it once, with `log::set_logger(&tagapp_engine::LOGGER)`. Until
/// [`channel`] sets the sink it drops everything. One process runs one
/// engine, so the sink and level are process-wide.
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
        if let Some(sink) = LOG_SINK.get().and_then(|s| s.lock().ok()?.clone()) {
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
/// `INFO`. Sets the `log` crate's global maximum too, so records below the
/// level are not even formatted. The page's level menu sends this as the
/// `set_log_level` command.
pub fn set_log_level(name: &str) {
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

#[cfg(test)]
mod tests {
    //! The prompt queue, the `ready` guard and the loop, without a UI.

    use super::*;
    use serde_json::json;

    #[test]
    fn prompts_queue_and_drop_stale_answers() {
        let mut prompts = Prompts::default();
        let first = prompts.ask("first");
        let second = prompts.ask("second");
        assert_eq!((first, second), (1, 2));
        // Only the front prompt can be answered.
        assert!(
            prompts
                .answer(&json!({ "id": second, "button": "ok" }))
                .is_none()
        );
        assert!(prompts.answer(&json!({ "button": "ok" })).is_none());
        let a = prompts
            .answer(&json!({ "id": first, "button": "ok", "index": 2, "path": "" }))
            .unwrap();
        assert_eq!(
            (a.kind, a.ok(), a.index, a.path),
            ("first", true, Some(2), None)
        );
        let a = prompts.answer(&json!({ "id": second })).unwrap();
        assert_eq!((a.kind, a.button.as_str()), ("second", "cancel"));
        assert!(prompts.is_empty());
        // An answer repeated after it was taken is stale.
        assert!(prompts.answer(&json!({ "id": second })).is_none());
        prompts.ask("third");
        prompts.remove_if(|k| *k == "third");
        assert!(prompts.front().is_none());
    }

    #[test]
    fn ready_starts_once_and_republishes_each_time() {
        let mut ready = Ready::default();
        assert!(!ready.started());
        assert!(ready.ready());
        assert!(ready.take_republish());
        assert!(!ready.take_republish());
        assert!(!ready.ready());
        assert!(ready.take_republish());
    }

    /// Counts `ready`s and posted messages; exits on `quit`.
    #[derive(Default)]
    struct Probe {
        /// The `ready` guard.
        ready: Ready,
        /// Starts seen.
        starts: u32,
        /// The sum of the posted messages.
        posted: u32,
        /// `quit` was sent.
        exit: bool,
    }

    impl EngineState for Probe {
        type Msg = u32;
        fn command(&mut self, cmd: &str, _args: &Value) {
            match cmd {
                "ready" => {
                    if self.ready.ready() {
                        self.starts += 1;
                    }
                }
                "quit" => self.exit = true,
                _ => {}
            }
        }
        fn query(&mut self, name: &str, _args: &Value) -> Value {
            match name {
                "posted" => json!(self.posted),
                other => query_error(&format!("unknown query {other}")),
            }
        }
        fn view_state(&mut self) -> Value {
            json!({ "starts": self.starts, "posted": self.posted })
        }
        fn message(&mut self, msg: u32) {
            self.posted += msg;
        }
        fn take_republish(&mut self) -> bool {
            self.ready.take_republish()
        }
        fn take_exit(&mut self) -> bool {
            std::mem::take(&mut self.exit)
        }
    }

    #[test]
    fn loop_sends_changes_republishes_on_ready_and_exits() {
        let events = Arc::new(Mutex::new(Vec::new()));
        let seen = events.clone();
        let (engine, poster, inbox) = channel(Duration::from_secs(10), move |e| {
            if !matches!(e, Event::Log { .. }) {
                seen.lock().unwrap().push(e);
            }
        });
        spawn("probe-engine", Probe::default(), inbox);
        engine.command("ready", json!({}));
        engine.command("ready", json!({}));
        assert!(poster.post(3));
        assert_eq!(engine.query("posted", json!({})), json!(3));
        assert_eq!(
            engine.query("nope", json!({}))["error"],
            "unknown query nope"
        );
        engine.command("quit", json!({}));
        assert_eq!(engine.query("posted", json!({})), json!(3));

        let events = events.lock().unwrap();
        let states: Vec<&Value> = events
            .iter()
            .filter_map(|e| match e {
                Event::State(s) => Some(s),
                _ => None,
            })
            .collect();
        // The first ready starts and sends; the second sends the same state
        // again; the post sends; the queries change nothing.
        assert_eq!(
            states,
            [
                &json!({ "starts": 1, "posted": 0 }),
                &json!({ "starts": 1, "posted": 0 }),
                &json!({ "starts": 1, "posted": 3 }),
            ]
        );
        assert_eq!(
            events.iter().filter(|e| matches!(e, Event::Exit)).count(),
            1
        );
    }
}
