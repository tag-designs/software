//! sensorViz's Tauri shell.
//!
//! It starts the engine, installs the logger, and connects the page to the
//! engine: two commands (`command`, `query`) in, two events (`state`, `log`)
//! out. Everything else is the engine's (`../core`) or the page's (`../ui`).
//!
//! `sensorviz [log.db3] [--load-preferences prefs.json]` opens a log and
//! loads a preference file at startup, as the Qt app's `--load-log` and
//! `--load-preferences` do. `SENSORVIZ_PREVIEW_DIALOG=visible` (or `sides`,
//! `colors`, `calibration`) opens that dialog once the log is drawn, for
//! screenshots. The shell is Tag Calibrator's
//! (`tagcalibrate-rs/app/src/main.rs`) without the tag.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::path::{Path, PathBuf};

use sensorviz_core::engine::{self, Engine, EngineConfig, Event};
use serde_json::{Value, json};
use tauri::{Emitter, Manager, State};

/// The page's `invoke("command", {cmd, args})`: queued for the engine thread;
/// the page learns the outcome from the next `state` event.
#[tauri::command]
fn command(engine: State<'_, Engine>, cmd: String, args: Value) {
    engine.command(&cmd, args);
}

/// The page's `invoke("query", {name, args})`. The engine answers on its own
/// thread, so the wait is kept off the main thread.
///
/// # Errors
///
/// Only when the blocking task itself fails; the join error's text is
/// returned to the page as the rejected promise.
#[tauri::command]
async fn query(engine: State<'_, Engine>, name: String, args: Value) -> Result<Value, String> {
    let engine = engine.inner().clone();
    tauri::async_runtime::spawn_blocking(move || engine.query(&name, args))
        .await
        .map_err(|e| e.to_string())
}

/// The published manual's sensorViz page, used when no built copy is found.
const ONLINE_GUIDE: &str = "https://tag-designs.github.io/software/user/apps/sensorviz.html";

/// The user manual's sensorViz page, searched for as Tag Monitor searches
/// for its own: `docs`, `host/docs/site` and `share/UltralightTags/docs`
/// beside the executable and up to five directories above it, then the
/// source tree's site. Returns a `file://` URL for a local copy, else
/// [`ONLINE_GUIDE`].
fn user_guide_url() -> String {
    let mut roots: Vec<PathBuf> = Vec::new();
    let exe = std::env::current_exe().unwrap_or_default();
    for dir in exe.ancestors().skip(1).take(6) {
        roots.push(dir.join("docs"));
        roots.push(dir.join("host/docs/site"));
        roots.push(dir.join("share/UltralightTags/docs"));
    }
    roots.push(Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../docs/site"));
    roots
        .iter()
        .flat_map(|r| {
            [
                r.join("apps/sensorviz.html"),
                r.join("apps/sensorviz/index.html"),
            ]
        })
        .find(|p| p.is_file())
        .and_then(|p| tauri::Url::from_file_path(p).ok())
        .map(String::from)
        .unwrap_or_else(|| ONLINE_GUIDE.to_owned())
}

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

/// Starts Tauri with the dialog and opener plugins, builds the
/// [`EngineConfig`], installs the engine's logger, spawns the engine and
/// registers the two commands.
///
/// # Panics
///
/// Panics (with "error while running sensorViz") if Tauri fails to start or
/// `setup` fails, e.g. when no home directory can be found.
fn main() {
    let (initial_log, initial_preferences) = startup_files();
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .setup(move |app| {
            let config = EngineConfig {
                home_dir: app.path().home_dir()?,
                user_guide_url: Some(user_guide_url()),
                initial_log,
                initial_preferences,
                preview_dialog: std::env::var("SENSORVIZ_PREVIEW_DIALOG").ok(),
            };
            let _ = log::set_logger(&engine::LOGGER);
            let handle = app.handle().clone();
            let engine = engine::spawn(config, move |event| match event {
                Event::State(state) => {
                    let _ = handle.emit("state", state);
                }
                Event::Log { level, text } => {
                    let _ = handle.emit("log", json!({ "level": level, "text": text }));
                }
            });
            app.manage(engine);
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![command, query])
        .run(tauri::generate_context!())
        .expect("error while running sensorViz");
}
