//! Tag Monitor's Tauri shell.
//!
//! It starts the engine, installs the logger that feeds the Error Log tab,
//! and connects the page to the engine: two commands (`command`, `query`)
//! in, two events (`state`, `log`) out. Everything else is the engine's
//! (`../core`) or the page's (`../ui`); see `../design/ui-engine-contract.md`.
//!
//! Closing the window is the engine's decision, since the configuration may
//! have unsaved changes: a close request is turned into the `quit` command,
//! and the window closes only when the engine sends [`Event::Exit`], after
//! the user has saved or discarded them and the tag has been detached.
//!
//! The settings (the recent configuration files) are `settings.json` under
//! the platform app config directory. The shell is Tag Programmer's
//! (`tagprogram-rs/app/src/main.rs`) with those two changes.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};

use serde_json::{Value, json};
use tagmonitor_core::engine::{self, Engine, EngineConfig, Event};
use tauri::{Emitter, Manager, State, WindowEvent};

/// The contract's `invoke("command", {cmd, args})`: queued for the engine
/// thread; the page learns the outcome from the next `state` event.
#[tauri::command]
fn command(engine: State<'_, Engine>, cmd: String, args: Value) {
    engine.command(&cmd, args);
}

/// The contract's `invoke("query", {name, args})`. The engine answers on its
/// own thread, so the wait is kept off the main thread.
///
/// # Errors
///
/// Only when the blocking task itself fails (panics or is cancelled); the
/// join error's text is returned to the page as the rejected promise.
#[tauri::command]
async fn query(engine: State<'_, Engine>, name: String, args: Value) -> Result<Value, String> {
    let engine = engine.inner().clone();
    tauri::async_runtime::spawn_blocking(move || engine.query(&name, args))
        .await
        .map_err(|e| e.to_string())
}

/// The published manual's page, used when no built copy is found.
const ONLINE_GUIDE: &str = "https://tag-designs.github.io/software/user/apps/tagmonitor.html";

/// The user manual's Tag Monitor page, searched for as Tag Programmer
/// searches for its own: `docs`, `host/docs/site` and
/// `share/UltralightTags/docs` beside the executable and up to five
/// directories above it, then the source tree's site, in both MkDocs URL
/// styles. Returns a `file://` URL for a local copy, else [`ONLINE_GUIDE`].
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
                r.join("apps/tagmonitor.html"),
                r.join("apps/tagmonitor/index.html"),
            ]
        })
        .find(|p| p.is_file())
        .and_then(|p| tauri::Url::from_file_path(p).ok())
        .map(String::from)
        .unwrap_or_else(|| ONLINE_GUIDE.to_owned())
}

/// Opts the whole process out of macOS App Nap, for its lifetime.
///
/// A tag sleeps after about 4.5 s without a host request, and the tag
/// thread's 400 ms status poll is what keeps an attached tag awake. App Nap
/// throttles the timers of an app whose window is in the background, which
/// can stretch that poll past the tag's limit (Tag Programmer found this;
/// see its `stay_awake`). The activity token is leaked on purpose. Does
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
/// [`EngineConfig`], installs the engine's logger, spawns the engine (which
/// attaches at once, as qtmonitor does) and registers the two commands.
///
/// # Panics
///
/// Panics (with "error while running Tag Monitor") if Tauri fails to start
/// or `setup` fails, e.g. when no config or home directory can be found.
fn main() {
    stay_awake();
    // Set once the engine allows the exit, so the close it causes is let
    // through.
    let exiting = Arc::new(AtomicBool::new(false));
    let exiting_close = exiting.clone();
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .setup(move |app| {
            let paths = app.path();
            let config = EngineConfig {
                settings_path: paths.app_config_dir()?.join("settings.json"),
                home_dir: paths.home_dir()?,
                user_guide_url: Some(user_guide_url()),
                auto_attach: std::env::var_os("TAGMONITOR_NO_ATTACH").is_none(),
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
        .expect("error while running Tag Monitor");
}
