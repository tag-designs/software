//! Tag Calibrator's Tauri shell.
//!
//! It starts the engine, installs the logger that feeds the Log tab, and
//! connects the page to the engine: two commands (`command`, `query`) in, two
//! events (`state`, `log`) out. Everything else is the engine's (`../core`)
//! or the page's (`../ui`). Closing the window becomes the `quit` command, so
//! the engine stops the stream and detaches first; the window closes when the
//! engine sends [`Event::Exit`].
//!
//! `TAGCALIBRATE_REPLAY=<capture.json>` replays a sample capture instead of
//! attaching to a tag, as qtcalibrate's `--replay-capture` does;
//! `TAGCALIBRATE_NO_ATTACH` opens without attaching. The shell is Tag
//! Monitor's (`tagmonitor-rs/app/src/main.rs`) with those two variables.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};

use serde_json::{Value, json};
use tagcalibrate_core::engine::{self, Engine, EngineConfig, Event};
use tauri::{Emitter, Manager, State, WindowEvent};

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

/// The published manual's page, used when no built copy is found. Until Tag
/// Calibrator has a page of its own, qtcalibrate's describes the same work.
const ONLINE_GUIDE: &str = "https://tag-designs.github.io/software/user/apps/qtcalibrate.html";

/// The user manual's calibration page, searched for as Tag Monitor searches
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
                r.join("apps/qtcalibrate.html"),
                r.join("apps/qtcalibrate/index.html"),
            ]
        })
        .find(|p| p.is_file())
        .and_then(|p| tauri::Url::from_file_path(p).ok())
        .map(String::from)
        .unwrap_or_else(|| ONLINE_GUIDE.to_owned())
}

/// Opts the process out of macOS App Nap, which would stretch the 100 ms
/// sample poll and the keep-awake reads of a background window past the
/// tag's sleep timeout. The activity token is leaked on purpose. Does
/// nothing on other platforms.
fn stay_awake() {
    #[cfg(target_os = "macos")]
    {
        use objc2_foundation::{NSActivityOptions, NSProcessInfo, NSString};
        let reason = NSString::from_str("Keeping an attached tag awake");
        let token = NSProcessInfo::processInfo().beginActivityWithOptions_reason(
            NSActivityOptions::UserInitiatedAllowingIdleSystemSleep,
            &reason,
        );
        std::mem::forget(token);
    }
}

/// Starts Tauri with the dialog and opener plugins, builds the
/// [`EngineConfig`], installs the engine's logger, spawns the engine and
/// registers the two commands.
///
/// # Panics
///
/// Panics (with "error while running Tag Calibrator") if Tauri fails to start
/// or `setup` fails, e.g. when no home directory can be found.
fn main() {
    stay_awake();
    let exiting = Arc::new(AtomicBool::new(false));
    let exiting_close = exiting.clone();
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .setup(move |app| {
            let replay = std::env::var_os("TAGCALIBRATE_REPLAY").map(PathBuf::from);
            let config = EngineConfig {
                home_dir: app.path().home_dir()?,
                user_guide_url: Some(user_guide_url()),
                auto_attach: replay.is_none()
                    && std::env::var_os("TAGCALIBRATE_NO_ATTACH").is_none(),
                replay,
                replay_period: engine::REPLAY_PERIOD,
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
                Event::Exit => {
                    exiting.store(true, Ordering::Relaxed);
                    handle.exit(0);
                }
            });
            app.manage(engine);
            Ok(())
        })
        .on_window_event(move |window, event| {
            if let WindowEvent::CloseRequested { api, .. } = event
                && !exiting_close.load(Ordering::Relaxed)
            {
                api.prevent_close();
                window.state::<Engine>().command("quit", json!({}));
            }
        })
        .invoke_handler(tauri::generate_handler![command, query])
        .run(tauri::generate_context!())
        .expect("error while running Tag Calibrator");
}
