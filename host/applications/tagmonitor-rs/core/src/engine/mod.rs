//! The engine: qtmonitor's `MainWindow` and `LiveTagSession`, with no UI.
//!
//! [`spawn`] starts the engine and returns an [`Engine`] handle. The UI talks
//! to it only through [`Engine::command`] and [`Engine::query`], and hears
//! from it only through the [`Event`] sink: the whole view state after every
//! change, log lines, and the request to exit. The protocol is
//! `design/ui-engine-contract.md`. The handle, the event type, the engine
//! loop, the prompt queue and the logger are the shared engine toolkit's
//! (`tagapp-engine`); this module supplies the state.
//!
//! # Threading
//!
//! Two threads, as in qtmonitor after its restructure
//! (`host/applications/qtmon/design/architecture.md`):
//!
//! - The **engine thread** (the toolkit's loop) owns all state, in one
//!   `Monitor`: the
//!   [`MonitorModel`](crate::model::monitormodel::MonitorModel), the
//!   [`ConfigDocument`](crate::model::configdocument::ConfigDocument), the
//!   outstanding prompts and the progress displays. It never touches the tag
//!   and never blocks on it, so the view stays live during a download.
//! - The **tag thread** (`tagthread`) owns the [`tagcore::Tag`]. It polls the
//!   status, runs the engine's commands in order, and runs downloads. Its
//!   reports are posted to the engine thread's channel through a
//!   [`Poster`](tagapp_engine::Poster).
//!
//! Everything that reaches the engine thread arrives on one channel, so
//! every state change happens there and needs no lock. After each message,
//! or the 1 s tick that notices a silent tag, the loop rebuilds the view
//! state (`view.rs`) and sends it only if it differs from the last one sent.
//!
//! # Logging
//!
//! [`UiLogger`] forwards the `log` crate's records -- the engine's own and
//! tagcore's -- to the sink as [`Event::Log`], filtered to the level the UI
//! chose with `set_log_level`.

pub mod download;
mod monitor;
mod tagthread;
mod view;

use std::path::PathBuf;
use std::sync::Arc;
use std::sync::atomic::AtomicBool;
use std::sync::mpsc;
use std::time::Duration;

use monitor::Monitor;

pub use tagapp_engine::{Engine, Event, LOGGER, UiLogger};

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

/// How long [`Engine::query`] waits for the engine thread, which never
/// waits on the tag.
const QUERY_TIMEOUT: Duration = Duration::from_secs(10);

/// Starts the engine and tag threads and returns the engine's handle.
/// `sink` receives every [`Event`].
///
/// Nothing happens until the page sends `ready`; then, with
/// [`EngineConfig::auto_attach`], the engine attaches, as qtmonitor does
/// when its window opens. A confirmed Quit detaches the tag first, and only
/// then sends [`Event::Exit`].
///
/// # Panics
///
/// Panics if the operating system cannot start a thread.
///
/// # Examples
///
/// ```no_run
/// use serde_json::json;
/// use tagmonitor_core::engine::{self, EngineConfig, Event};
///
/// let config = EngineConfig {
///     settings_path: "/tmp/tagmonitor-scratch/settings.json".into(),
///     home_dir: "/tmp/tagmonitor-scratch".into(),
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
pub fn spawn(config: EngineConfig, sink: impl Fn(Event) + Send + Sync + 'static) -> Engine {
    let (engine, poster, inbox) = tagapp_engine::channel(QUERY_TIMEOUT, sink);
    let (tag_tx, tag_rx) = mpsc::channel();
    let cancel = Arc::new(AtomicBool::new(false));
    {
        let cancel = cancel.clone();
        std::thread::Builder::new()
            .name("tagmonitor-tag".into())
            .spawn(move || tagthread::run(tag_rx, poster, cancel))
            .expect("tag thread");
    }
    tagapp_engine::spawn(
        "tagmonitor-engine",
        Monitor::new(config, tag_tx, cancel),
        inbox,
    );
    engine
}
