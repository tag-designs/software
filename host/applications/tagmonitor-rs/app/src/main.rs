//! Tag Monitor's Tauri shell.
//!
//! The shared shell (`tagapp-shell`) does the work: the `command` and `query`
//! Tauri commands, the `state` and `log` events, App Nap, the user guide, and
//! closing through the engine. A window close or Cmd-Q becomes the engine's
//! `quit`, since the configuration may have unsaved changes; the window
//! closes when the engine sends [`Event::Exit`], after the user has saved or
//! discarded them and the tag has been detached -- or after a few seconds
//! without the detach, for a tag that no longer answers. Everything else is
//! the engine's (`../core`) or the page's (`../ui`); see
//! `../design/ui-engine-contract.md`.
//!
//! This file builds the engine: the settings (the recent configuration
//! files) are `settings.json` under the platform app config directory, and
//! `TAGMONITOR_NO_ATTACH` opens without attaching.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use tagapp_shell::{App, Engine};
use tagmonitor_core::engine::{self, EngineConfig, Event};

/// Runs Tag Monitor: builds the [`EngineConfig`], installs the engine's
/// logger and spawns the engine (which attaches at once, as qtmonitor does),
/// forwarding its events to the page.
///
/// # Panics
///
/// Panics (with "error while running Tag Monitor") if Tauri fails to start
/// or no config or home directory can be found.
fn main() {
    let app = App {
        name: "Tag Monitor",
        guide_page: "tagmonitor",
        keep_awake: true,
        close_through_engine: true,
    };
    tagapp_shell::run(app, tauri::generate_context!(), |setup| {
        let paths = tauri::Manager::path(setup.app);
        let config = EngineConfig {
            settings_path: paths.app_config_dir()?.join("settings.json"),
            home_dir: paths.home_dir()?,
            user_guide_url: Some(setup.user_guide_url),
            auto_attach: std::env::var_os("TAGMONITOR_NO_ATTACH").is_none(),
        };
        let _ = log::set_logger(&engine::LOGGER);
        let events = setup.events;
        let engine = engine::spawn(config, move |event| match event {
            Event::State(state) => events.state(state),
            Event::Log { level, text } => events.log(level, &text),
            Event::Exit => events.exit(),
        });
        Ok(Engine::new(engine, engine::Engine::command, engine::Engine::query))
    });
}
