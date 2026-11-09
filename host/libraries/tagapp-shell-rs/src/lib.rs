//! The Tauri shell shared by Tag Programmer, Tag Monitor, Tag Calibrator and
//! Tag View.
//!
//! Each application's `main` describes itself ([`App`]), passes in its
//! `tauri::generate_context!()` (which must expand in the application's own
//! crate, since it reads that crate's `tauri.conf.json`), and gives a
//! function that builds its engine's configuration and spawns the engine
//! with the sink [`Setup::sink`] gives. Every engine is the shared engine
//! toolkit's (`tagapp-engine`), so [`run`] does the rest, the same way for
//! every application:
//!
//! - the toolkit's logger, installed before the engine starts, so the
//!   engine's and its libraries' `log` records reach the page;
//! - the page's two commands, `invoke("command", {cmd, args})` and
//!   `invoke("query", {name, args})`, forwarded to the engine's
//!   [`Engine`]; a query waits off Tauri's main thread;
//! - the engine's events ([`Event`]): `State` and `Log` emitted to the page
//!   as the `state` and `log` events, `Exit` ending the application;
//! - with [`App::close_through_engine`], a window close and the macOS Quit
//!   item (Cmd-Q) become the engine's `quit` command, and the window closes
//!   when the engine sends [`Event::Exit`]. Tauri's default macOS Quit sends
//!   Cocoa's `terminate:`, which tao ends from `applicationWillTerminate`
//!   with no close request and no exit request that could be prevented, so
//!   the shell installs Tauri's default menu with its own Quit item instead.
//!   The Dock's Quit and a log-out still end the process directly;
//! - with [`App::keep_awake`], the process opts out of macOS App Nap, so an
//!   attached tag's 400 ms status poll is not stretched past the tag's
//!   4.5 s limit while the window is in the background;
//! - the user manual's page for the application, found locally or online
//!   ([`user_guide_url`]).
//!
//! The dialog and opener plugins are registered here; the page calls them.
//! Each application must still depend on both plugin crates directly, so that
//! its build script (through `tagapp-shell-build`) can resolve the plugins'
//! permissions. The build-script half is `tagapp-shell-build`.
//!
//! See `host/design/proposals/unified-app-shell.md`.

use std::error::Error;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};

use serde_json::{Value, json};
use tagapp_engine::{Engine, Event};
use tauri::{AppHandle, Emitter, Manager, State, WindowEvent, Wry};

/// What an application tells the shell about itself.
#[derive(Debug, Clone, Copy)]
pub struct App {
    /// The application's name, as the window and messages give it (`"Tag
    /// Monitor"`).
    pub name: &'static str,
    /// The manual's page for it, without extension (`"tagmonitor"`): looked
    /// for as `apps/<page>.html` and `apps/<page>/index.html`.
    pub guide_page: &'static str,
    /// Opt out of macOS App Nap for the process's lifetime. Set by every
    /// application that keeps a tag attached.
    pub keep_awake: bool,
    /// Turn a window close and the macOS Quit item into the engine's `quit`
    /// command; the window then closes only when the engine sends
    /// [`Event::Exit`]. Set by every application whose engine has something
    /// to ask or finish first (unsaved work, a tag to detach, programming in
    /// progress). When clear, a close closes at once and the default macOS
    /// menu is kept.
    pub close_through_engine: bool,
}

/// Where an engine's events go: the page, and the shell's exit.
///
/// Cloned into the engine's event sink, which runs on the engine thread (and,
/// for a log record, on whatever thread logged), so it is cheap and never
/// waits on the engine.
#[derive(Clone)]
struct Events {
    /// The running application.
    handle: AppHandle<Wry>,
    /// Set once the engine allows the exit, so a close request that arrives
    /// after it is let through rather than turned into another `quit`.
    exiting: Arc<AtomicBool>,
}

impl Events {
    /// Passes one engine event on: `State` as the `state` event, `Log` as
    /// the `log` event `{level, text}`, and `Exit` by ending the
    /// application. A failure to emit (the window is gone) is ignored.
    fn send(&self, event: Event) {
        match event {
            Event::State(state) => {
                let _ = self.handle.emit("state", state);
            }
            Event::Log { level, text } => {
                let _ = self
                    .handle
                    .emit("log", json!({ "level": level, "text": text }));
            }
            Event::Exit => {
                self.exiting.store(true, Ordering::Relaxed);
                self.handle.exit(0);
            }
        }
    }
}

/// What the application's start function gets: the running app (for its
/// platform paths), the user guide's URL, and the sink its engine sends
/// events to.
pub struct Setup<'a> {
    /// The running Tauri application; `app.path()` gives the platform
    /// directories.
    pub app: &'a tauri::App<Wry>,
    /// The manual's page for the application.
    guide_url: String,
    /// Where the engine's events go.
    events: Events,
}

impl Setup<'_> {
    /// The manual's page for the application ([`user_guide_url`]), for the
    /// engine's Help.
    pub fn user_guide_url(&self) -> String {
        self.guide_url.clone()
    }

    /// The event sink to spawn the engine with: it forwards every [`Event`]
    /// to the page or, for [`Event::Exit`], ends the application.
    pub fn sink(&self) -> impl Fn(Event) + Send + Sync + 'static {
        let events = self.events.clone();
        move |event| events.send(event)
    }
}

/// The `invoke("command", {cmd, args})` command: queued for the engine
/// thread; the page learns the outcome from the next `state` event.
#[tauri::command]
fn command(engine: State<'_, Engine>, cmd: String, args: Value) {
    engine.command(&cmd, args);
}

/// The `invoke("query", {name, args})` command. The engine answers on its own
/// thread, so the wait is kept off Tauri's main thread.
///
/// # Errors
///
/// Only when the blocking task itself fails (panics or is cancelled); the
/// join error's text is returned to the page as the rejected promise.
#[tauri::command]
async fn query(engine: State<'_, Engine>, name: String, args: Value) -> Result<Value, String> {
    let engine = Engine::clone(&engine);
    tauri::async_runtime::spawn_blocking(move || engine.query(&name, args))
        .await
        .map_err(|e| e.to_string())
}

/// The id of the Quit item [`app_menu`] puts in the application menu.
#[cfg(target_os = "macos")]
const QUIT_MENU_ID: &str = "quit";

/// Tauri's default macOS menu with its Quit item replaced by one that sends
/// the engine `quit` (see the crate documentation for why).
///
/// # Errors
///
/// When Tauri cannot build or change the menu.
#[cfg(target_os = "macos")]
fn app_menu(app: &AppHandle<Wry>) -> tauri::Result<tauri::menu::Menu<Wry>> {
    use tauri::menu::{Menu, MenuItem, MenuItemKind};
    let menu = Menu::default(app)?;
    if let Some(MenuItemKind::Submenu(app_submenu)) = menu.items()?.into_iter().next() {
        let items = app_submenu.items()?;
        if let Some(MenuItemKind::Predefined(_)) = items.last() {
            app_submenu.remove_at(items.len() - 1)?;
        }
        let quit = format!("Quit {}", app.package_info().name);
        app_submenu.append(&MenuItem::with_id(
            app,
            QUIT_MENU_ID,
            quit,
            true,
            Some("CmdOrCtrl+Q"),
        )?)?;
    }
    Ok(menu)
}

/// Opts the whole process out of macOS App Nap, for its lifetime.
///
/// A tag sleeps after about 4.5 s without a host request, and the 400 ms
/// status poll is what keeps an attached tag awake. App Nap throttles the
/// timers of an app whose window is in the background, which stretched that
/// poll past the tag's limit in Tag Programmer. A user-initiated activity is
/// the documented opt-out; it still lets the system sleep when idle. The
/// activity token is leaked on purpose. Does nothing on other platforms.
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

/// The user manual's page `apps/<page>`, searched for as the Qt applications
/// searched: `docs`, `host/docs/site` and `share/UltralightTags/docs` beside
/// the executable and up to five directories above it (the packaged layouts
/// and a build tree), then the source tree's `host/docs/site`, in both MkDocs
/// URL styles (`apps/<page>.html`, `apps/<page>/index.html`).
///
/// Returns a `file://` URL for a local copy, else the published manual's
/// page.
pub fn user_guide_url(page: &str) -> String {
    let mut roots: Vec<PathBuf> = Vec::new();
    let exe = std::env::current_exe().unwrap_or_default();
    for dir in exe.ancestors().skip(1).take(6) {
        roots.push(dir.join("docs"));
        roots.push(dir.join("host/docs/site"));
        roots.push(dir.join("share/UltralightTags/docs"));
    }
    // This crate is host/libraries/tagapp-shell-rs; the site is host/docs/site.
    roots.push(Path::new(env!("CARGO_MANIFEST_DIR")).join("../../docs/site"));
    roots
        .iter()
        .flat_map(|r| {
            [
                r.join(format!("apps/{page}.html")),
                r.join(format!("apps/{page}/index.html")),
            ]
        })
        .find(|p| p.is_file())
        .and_then(|p| tauri::Url::from_file_path(p).ok())
        .map(String::from)
        .unwrap_or_else(|| format!("https://tag-designs.github.io/software/user/apps/{page}.html"))
}

/// Runs the application: the toolkit's logger, Tauri with the dialog and
/// opener plugins, the two commands, the close and Quit handling [`App`]
/// asks for, and the engine `start` spawns.
///
/// `start` runs once, in Tauri's setup, on the main thread. It builds the
/// engine's configuration (from `setup.app.path()` and the like), spawns the
/// engine with [`Setup::sink`], and returns the engine's handle.
///
/// # Panics
///
/// Panics with "error while running `name`" (the [`App`]'s name) if Tauri fails to start or
/// `start` returns an error (e.g. no config or home directory).
pub fn run<F>(app: App, context: tauri::Context<Wry>, start: F)
where
    F: FnOnce(Setup<'_>) -> Result<Engine, Box<dyn Error>> + Send + 'static,
{
    // Ignored if a logger is already set. The toolkit's logger drops records
    // until the engine's channel gives it the sink.
    let _ = log::set_logger(&tagapp_engine::LOGGER);
    if app.keep_awake {
        stay_awake();
    }
    let exiting = Arc::new(AtomicBool::new(false));
    let exiting_close = exiting.clone();
    let builder = tauri::Builder::default();
    #[cfg(target_os = "macos")]
    let builder = if app.close_through_engine {
        builder.menu(app_menu).on_menu_event(|handle, event| {
            if event.id() == QUIT_MENU_ID {
                handle.state::<Engine>().command("quit", json!({}));
            }
        })
    } else {
        builder
    };
    builder
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .setup(move |tauri_app| {
            let events = Events {
                handle: tauri_app.handle().clone(),
                exiting,
            };
            let engine = start(Setup {
                app: tauri_app,
                guide_url: user_guide_url(app.guide_page),
                events,
            })?;
            tauri_app.manage(engine);
            Ok(())
        })
        .on_window_event(move |window, event| {
            if let WindowEvent::CloseRequested { api, .. } = event
                && app.close_through_engine
                && !exiting_close.load(Ordering::Relaxed)
            {
                api.prevent_close();
                window.state::<Engine>().command("quit", json!({}));
            }
        })
        .invoke_handler(tauri::generate_handler![command, query])
        .run(context)
        .unwrap_or_else(|e| panic!("error while running {}: {e}", app.name));
}
