//! Tag Programmer's Tauri shell.
//!
//! It starts the engine, installs the logger that feeds the log pane, and
//! connects the page to the engine: two commands (`command`, `query`) in, two
//! events (`state`, `log`) out. Everything else is the engine's
//! (`../core`) or the page's (`../ui`); see
//! `../design/ui-engine-contract.md`.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde_json::{Value, json};
use tagprogram_core::engine::{self, Engine, EngineConfig, Event};
use tauri::{Emitter, Manager, State};

/// The contract's `invoke("command", {cmd, args})`.
#[tauri::command]
fn command(engine: State<'_, Engine>, cmd: String, args: Value) {
    engine.command(&cmd, args);
}

/// The contract's `invoke("query", {name, args})`. The engine answers on its
/// own thread, so the wait is kept off the main thread.
#[tauri::command]
async fn query(engine: State<'_, Engine>, name: String, args: Value) -> Result<Value, String> {
    let engine = engine.inner().clone();
    tauri::async_runtime::spawn_blocking(move || engine.query(&name, args))
        .await
        .map_err(|e| e.to_string())
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .setup(|app| {
            let paths = app.path();
            let config = EngineConfig {
                cache_dir: paths.app_data_dir()?.join("firmware"),
                settings_path: paths.app_config_dir()?.join("settings.json"),
                documents_dir: paths.document_dir()?,
                user_guide_url: None,
            };
            let _ = log::set_logger(&engine::LOGGER);
            let handle = app.handle().clone();
            let engine = engine::spawn(config, move |event| {
                let _ = match event {
                    Event::State(state) => handle.emit("state", state),
                    Event::Log { level, text } => {
                        handle.emit("log", json!({ "level": level, "text": text }))
                    }
                };
            });
            app.manage(engine);
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![command, query])
        .run(tauri::generate_context!())
        .expect("error while running Tag Programmer");
}
