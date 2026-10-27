//! Tag Programmer's Tauri shell.
//!
//! It starts the engine, installs the logger that feeds the log pane, and
//! connects the page to the engine: two commands (`command`, `query`) in, two
//! events (`state`, `log`) out. Everything else is the engine's
//! (`../core`) or the page's (`../ui`); see
//! `../design/ui-engine-contract.md`.
//!
//! The engine runs on its own thread ([`engine::spawn`]) and is handed to
//! Tauri as managed state; events it raises are forwarded with
//! [`Emitter::emit`] from that thread. Where the engine keeps its files:
//!
//! - firmware cache: `firmware/` under the platform app data directory;
//! - settings: `settings.json` under the platform app config directory;
//! - record default: under the user's Documents directory (the engine adds
//!   `Tag Programmer/programmed-tags.csv`).
//!
//! The build-time side (staging the page, `TAURI_CONFIG`) is in `build.rs`;
//! packaging is described in `../design/build-and-packaging.md`.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::path::{Path, PathBuf};

use serde_json::{Value, json};
use tagprogram_core::engine::{self, Engine, EngineConfig, Event};
use tauri::{Emitter, Manager, State};

/// The contract's `invoke("command", {cmd, args})`.
///
/// Fire and forget: the command is queued for the engine thread and the
/// page learns the outcome from the next `state` event. Returns to the page
/// as `null`; an unknown command is the engine's to report.
#[tauri::command]
fn command(engine: State<'_, Engine>, cmd: String, args: Value) {
    engine.command(&cmd, args);
}

/// The contract's `invoke("query", {name, args})`. The engine answers on its
/// own thread, so the wait is kept off the main thread.
///
/// The answer is whatever JSON the engine returns for `name`; see the
/// contract for each query.
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
const ONLINE_GUIDE: &str = "https://tag-designs.github.io/software/user/apps/tagprogram.html";

/// The user manual's Tag Programmer page, searched for as the Qt
/// applications search (`host/common/qtuserguide.h`): `docs`,
/// `host/docs/site` and `share/UltralightTags/docs` beside the executable and
/// up to five directories above it (the packaged layouts and a build tree),
/// then the source tree's site; else the published manual.
///
/// In each root both MkDocs URL styles are tried: `apps/tagprogram.html` and
/// `apps/tagprogram/index.html`. The source-tree root is the
/// `host/docs/site` of the tree the binary was compiled from, which only
/// exists for a developer build after the docs were built. Returns a
/// `file://` URL for a local copy, else [`ONLINE_GUIDE`].
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
        .flat_map(|r| [r.join("apps/tagprogram.html"), r.join("apps/tagprogram/index.html")])
        .find(|p| p.is_file())
        .and_then(|p| tauri::Url::from_file_path(p).ok())
        .map(String::from)
        .unwrap_or_else(|| ONLINE_GUIDE.to_owned())
}

/// Opts the whole process out of macOS App Nap, for its lifetime.
///
/// A tag sleeps after about 4.5 s without a host request (its state machine
/// does this on purpose), and the engine's 400 ms status poll is what keeps
/// an attached tag awake. App Nap throttles the timers of an app whose window
/// is in the background or covered, which stretched that poll past the
/// tag's limit: the tag fell asleep while the window still showed IDLE, and
/// the next Program failed with "The tag stopped answering". A
/// user-initiated activity is the documented opt-out; it still allows the
/// system to sleep when idle. The activity token is leaked on purpose, so
/// the opt-out lasts until the process exits. Does nothing on other
/// platforms.
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
/// [`EngineConfig`] from the platform directories, installs the engine's
/// logger, spawns the engine and registers the two commands.
///
/// # Panics
///
/// Panics (with "error while running Tag Programmer") if Tauri fails to
/// start or `setup` fails, e.g. when no app data, config or home directory
/// can be determined.
fn main() {
    stay_awake();
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .setup(|app| {
            let paths = app.path();
            let config = EngineConfig {
                cache_dir: paths.app_data_dir()?.join("firmware"),
                settings_path: paths.app_config_dir()?.join("settings.json"),
                // XDG names no Documents folder on a minimal Linux setup
                // (or a clean container); fall back to ~/Documents, as
                // qtprogram's QStandardPaths does.
                documents_dir: paths
                    .document_dir()
                    .or_else(|_| paths.home_dir().map(|home| home.join("Documents")))?,
                user_guide_url: Some(user_guide_url()),
            };
            // Ignored if a logger is already set; the engine's logger feeds
            // the page's log pane through Event::Log.
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
