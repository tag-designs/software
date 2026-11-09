//! Tag Calibrator's Tauri shell.
//!
//! The shared shell (`tagapp-shell`) does the work: the `command` and `query`
//! Tauri commands, the `state` and `log` events, App Nap, the user guide, and
//! closing through the engine. A window close or Cmd-Q becomes the engine's
//! `quit`, so the engine stops the stream and detaches first; the window
//! closes when the engine sends `Event::Exit`. Everything else is the
//! engine's (`../core`) or the page's (`../ui`).
//!
//! This file builds the engine. `TAGCALIBRATE_REPLAY=<capture.json>` replays
//! a sample capture instead of attaching to a tag, as qtcalibrate's
//! `--replay-capture` does; `TAGCALIBRATE_NO_ATTACH` opens without attaching.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::path::PathBuf;

use tagapp_shell::App;
use tagcalibrate_core::engine::{self, EngineConfig};

/// Runs Tag Calibrator: builds the [`EngineConfig`] and spawns the engine
/// with the shell's event sink.
///
/// # Panics
///
/// Panics (with "error while running Tag Calibrator") if Tauri fails to
/// start or no home directory can be found.
fn main() {
    let app = App {
        name: "Tag Calibrator",
        guide_page: "tagcalibrate",
        keep_awake: true,
        close_through_engine: true,
    };
    tagapp_shell::run(app, tauri::generate_context!(), |setup| {
        let replay = std::env::var_os("TAGCALIBRATE_REPLAY").map(PathBuf::from);
        let config = EngineConfig {
            home_dir: tauri::Manager::path(setup.app).home_dir()?,
            user_guide_url: Some(setup.user_guide_url()),
            auto_attach: replay.is_none() && std::env::var_os("TAGCALIBRATE_NO_ATTACH").is_none(),
            replay,
            replay_period: engine::REPLAY_PERIOD,
        };
        Ok(engine::spawn(config, setup.sink()))
    });
}
