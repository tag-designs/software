//! Tag View's Tauri shell: sensorViz in Rust, installed as `tagview` beside
//! the Qt `sensorviz` while both ship.
//!
//! The shared shell (`tagapp-shell`) does the work: the `command` and `query`
//! Tauri commands, the `state` and `log` events and the user guide. Tag View
//! attaches no tag and has no unsaved work, so a close closes at once and it
//! does not opt out of App Nap. Everything else is the engine's (`../core`)
//! or the page's (`../ui`).
//!
//! `tagview [log.db3] [--load-preferences prefs.json]` opens a log and
//! loads a preference file at startup, as the Qt app's `--load-log` and
//! `--load-preferences` do. `SENSORVIZ_PREVIEW_DIALOG=visible` (or `sides`,
//! `colors`, `calibration`) opens that dialog once the log is drawn, for
//! screenshots.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::path::PathBuf;

use sensorviz_core::engine::{self, EngineConfig, Event};
use tagapp_shell::{App, Engine};

/// The log and preference file named on the command line: the first
/// argument not starting with `-`, and the one after `--load-preferences`.
fn startup_files() -> (Option<PathBuf>, Option<PathBuf>) {
    let mut log = None;
    let mut preferences = None;
    let mut args = std::env::args_os().skip(1);
    while let Some(arg) = args.next() {
        if arg == "--load-preferences" {
            preferences = args.next().map(PathBuf::from);
        } else if !arg.to_string_lossy().starts_with('-') && log.is_none() {
            log = Some(PathBuf::from(arg));
        }
    }
    (log, preferences)
}

/// Runs Tag View: builds the [`EngineConfig`] (with the files named on the
/// command line), installs the engine's logger and spawns the engine,
/// forwarding its events to the page.
///
/// # Panics
///
/// Panics (with "error while running Tag View") if Tauri fails to start or
/// no home directory can be found.
fn main() {
    let (initial_log, initial_preferences) = startup_files();
    let app = App {
        name: "Tag View",
        guide_page: "tagview",
        keep_awake: false,
        close_through_engine: false,
    };
    tagapp_shell::run(app, tauri::generate_context!(), move |setup| {
        let config = EngineConfig {
            home_dir: tauri::Manager::path(setup.app).home_dir()?,
            user_guide_url: Some(setup.user_guide_url),
            initial_log,
            initial_preferences,
            preview_dialog: std::env::var("SENSORVIZ_PREVIEW_DIALOG").ok(),
        };
        let _ = log::set_logger(&engine::LOGGER);
        let events = setup.events;
        let engine = engine::spawn(config, move |event| match event {
            Event::State(state) => events.state(state),
            Event::Log { level, text } => events.log(level, &text),
        });
        Ok(Engine::new(engine, engine::Engine::command, engine::Engine::query))
    });
}
