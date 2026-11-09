//! The engine: qtprogram's `Station` and `ProgramSession`, with no UI.
//!
//! [`spawn`] starts the engine on its own thread and returns an [`Engine`]
//! handle. The UI talks to it only through [`Engine::command`] and
//! [`Engine::query`], and hears from it only through the [`Event`] sink: the
//! whole view state after every change, log lines, and the request to exit.
//! The protocol is `design/ui-engine-contract.md`; the internals are
//! `design/engine.md`. The handle, the event type, the engine loop, the
//! prompt queue, the `ready` guard and the logger are the shared engine
//! toolkit's (`tagapp-engine`); this module supplies the state.
//!
//! # Threading
//!
//! The engine thread (the toolkit's loop) owns all state, in one `Station`;
//! nothing else ever holds a reference to it. Everything that reaches the
//! station arrives on one channel:
//!
//! - commands and queries from the UI, through [`Engine`];
//! - `Done` closures from worker threads (and, once, from the station
//!   itself), posted through a [`Poster`](tagapp_engine::Poster) and applied
//!   to the station on the engine thread, so that every state change happens
//!   there and needs no lock. The one shared object is the firmware catalog,
//!   an `Arc<Catalog>` that worker threads refresh and fetch through; it
//!   guards its own state with interior mutexes.
//!
//! Unlike Tag Monitor and Tag Calibrator there is no tag thread: the engine
//! thread owns the `Tag`, because the programming step detaches tagcore and
//! hands the base to a probe-rs worker in consecutive statements
//! (`Station::run_programmer`).
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
//! (`view.rs`) and sends it only if it differs from the last one sent, or
//! after a `ready`.
//!
//! # Modules
//!
//! - `station`: the eight checks, base selection, attach and polling, the
//!   firmware choice, the record file and prompts (`station.cpp`).
//! - `session`: the program and retest step machine
//!   (`programsession.cpp`, and `Station::program`/`retest`/`writeRecord`).
//! - `view`: the view-state JSON, with `MainWindow`'s display rules
//!   (`mainwindow.cpp`), and the menus.
//!
//! # Logging
//!
//! [`UiLogger`] forwards the `log` crate's records -- the engine's own and
//! those of `tagcore` and `tagprobe` (probe-rs progress) -- to the sink as
//! [`Event::Log`], filtered to the level the UI chose with `set_log_level`.

mod session;
mod station;
mod view;

use std::path::PathBuf;
use std::time::Duration;

use station::Station;

pub use tagapp_engine::{Engine, Event, LOGGER, UiLogger};

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

/// A worker's result or progress: a closure applied to the station on the
/// engine thread. The station also posts one to itself to defer opening the
/// record until after the first view is published (`Station::begin_checks`).
pub(crate) type Done = Box<dyn FnOnce(&mut Station) + Send>;

/// How long [`Engine::query`] waits. The engine thread answers between other
/// messages, and those can include synchronous monitor traffic to the tag.
const QUERY_TIMEOUT: Duration = Duration::from_secs(10);

/// Starts the engine thread and returns its handle. `sink` receives every
/// [`Event`].
///
/// Builds the `Station` on the calling thread, which loads the settings file
/// and the cached release list, then starts the toolkit's loop on it.
/// Nothing else happens until the UI sends `ready`: the first-run prompt,
/// the checks and the catalog refresh all wait for it.
///
/// # Panics
///
/// Panics if the operating system cannot start a thread.
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
/// engine.command("ready", json!({}));
/// let about = engine.query("about", json!({}));
/// println!("{}", about["title"]);
/// ```
pub fn spawn(config: EngineConfig, sink: impl Fn(Event) + Send + Sync + 'static) -> Engine {
    let (engine, poster, inbox) = tagapp_engine::channel(QUERY_TIMEOUT, sink);
    tagapp_engine::spawn("tagprogram-engine", Station::new(config, poster), inbox);
    engine
}
