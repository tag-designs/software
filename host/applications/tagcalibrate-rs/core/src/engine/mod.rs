//! The engine: qtcalibrate's `MainWindow` behaviour, with no UI.
//!
//! [`spawn`] starts the engine and returns an [`Engine`] handle. The UI talks
//! to it only through [`Engine::command`] and [`Engine::query`], and hears
//! from it only through the [`Event`] sink: the whole view state after every
//! change, log lines, and the request to exit. The handle, the event type,
//! the engine loop, the prompt queue and the logger are the shared engine
//! toolkit's (`tagapp-engine`); this module supplies the state.
//!
//! # Threading
//!
//! Two threads, as in Tag Monitor (`tagmonitor-rs/core/src/engine`):
//!
//! - The **engine thread** (the toolkit's loop) owns all state, in one
//!   `Calibrator`: the calibration session, the sample capture, the display
//!   settings, the prompts and the timers. It never touches the tag.
//! - The **tag thread** (`tagthread`) owns the [`tagcore::Tag`]. It runs the
//!   engine's commands in order and, while streaming, reads the calibration
//!   log every 100 ms, as qtcalibrate's sample timer does. Its reports are
//!   posted to the engine thread's channel through a
//!   [`Poster`](tagapp_engine::Poster).
//!
//! A replay ([`EngineConfig::replay`]) needs no tag: the engine feeds a
//! capture's samples itself, one per 100 ms tick, as qtcalibrate's
//! `--replay-capture` does.
//!
//! # Logging
//!
//! [`UiLogger`] forwards the `log` crate's records -- the engine's own and
//! tagcore's -- to the sink as [`Event::Log`], filtered to the level the UI
//! chose with `set_log_level`. Each quality update logs the four `DEBUG` lines qtcalibrate logs
//! (`magquality:`, `residual:`, `gravity:`, `retention:`), in its format, so
//! a replay's log can be compared with qtcalibrate's line for line.

mod calibrator;
mod capture;
mod tagthread;
mod view;

use std::path::PathBuf;
use std::sync::mpsc;
use std::time::Duration;

use calibrator::Calibrator;

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

/// How long [`Engine::query`] waits for the engine thread, which never
/// waits on the tag.
const QUERY_TIMEOUT: Duration = Duration::from_secs(10);

pub use tagapp_engine::{Engine, Event, LOGGER, UiLogger};

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
    let (engine, poster, inbox) = tagapp_engine::channel(QUERY_TIMEOUT, sink);
    let (tag_tx, tag_rx) = mpsc::channel();
    std::thread::Builder::new()
        .name("tagcalibrate-tag".into())
        .spawn(move || tagthread::run(tag_rx, poster))
        .expect("tag thread");
    let calibrator = Calibrator::new(config, tag_tx).expect("calibration session");
    tagapp_engine::spawn("tagcalibrate-engine", calibrator, inbox);
    engine
}
