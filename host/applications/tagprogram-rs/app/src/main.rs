//! Tag Programmer's Tauri shell.
//!
//! The shared shell (`tagapp-shell`) does the work: the `command` and `query`
//! Tauri commands, the `state` and `log` events, App Nap, the user guide, and
//! closing through the engine. A window close or Cmd-Q becomes the engine's
//! `quit`, which quits at once unless a tag is being programmed, when it asks
//! first; the window closes when the engine sends `Event::Exit`.
//! Everything else is the engine's (`../core`) or the page's (`../ui`); see
//! `../design/ui-engine-contract.md`.
//!
//! This file builds the engine. Where it keeps its files:
//!
//! - firmware cache: `firmware/` under the platform app data directory;
//! - settings: `settings.json` under the platform app config directory;
//! - record default: under the user's Documents directory (the engine adds
//!   `Tag Programmer/programmed-tags.csv`).
//!
//! Packaging is described in `../design/build-and-packaging.md`.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use tagapp_shell::App;
use tagprogram_core::engine::{self, EngineConfig};

/// Runs Tag Programmer: builds the [`EngineConfig`] from the platform
/// directories and spawns the engine with the shell's event sink.
///
/// # Panics
///
/// Panics (with "error while running Tag Programmer") if Tauri fails to
/// start or no app data, config or home directory can be determined.
fn main() {
    let app = App {
        name: "Tag Programmer",
        guide_page: "tagprogram",
        keep_awake: true,
        close_through_engine: true,
    };
    tagapp_shell::run(app, tauri::generate_context!(), |setup| {
        let paths = tauri::Manager::path(setup.app);
        let config = EngineConfig {
            cache_dir: paths.app_data_dir()?.join("firmware"),
            settings_path: paths.app_config_dir()?.join("settings.json"),
            // XDG names no Documents folder on a minimal Linux setup (or a
            // clean container); fall back to ~/Documents, as qtprogram's
            // QStandardPaths does.
            documents_dir: paths
                .document_dir()
                .or_else(|_| paths.home_dir().map(|home| home.join("Documents")))?,
            user_guide_url: Some(setup.user_guide_url()),
        };
        Ok(engine::spawn(config, setup.sink()))
    });
}
