---
type: readme
status: current
summary: "tagapp-shell, the Tauri shell the four Rust applications share -- the engine toolkit's logger, the command and query Tauri commands, the state and log events, closing and Cmd-Q through the engine, the App Nap opt-out and the user guide search -- what an application passes it, and how to build and check it."
---

# tagapp-shell (Rust)

The Tauri shell of Tag Programmer, Tag Monitor, Tag Calibrator and Tag View.
Each application's `app/src/main.rs` describes itself, passes its
`tauri::generate_context!()`, and gives a closure that builds its engine's
configuration and spawns the engine with the sink the shell supplies. Every
engine is the shared engine toolkit's
([`tagapp-engine`](../tagapp-engine-rs/README.md)), so the shell runs the
toolkit's `Engine` directly, and `tagapp_shell::run` does the rest, the same
way for every application:

- the toolkit's logger (`tagapp_engine::LOGGER`), installed with
  `log::set_logger` before Tauri starts, so the engine's and its libraries'
  `log` records reach the page's log pane;
- the page's two Tauri commands, `invoke("command", {cmd, args})` and
  `invoke("query", {name, args})`, forwarded to the engine (a query waits on
  Tauri's blocking pool, off the main thread);
- the engine's events: `Event::State` and `Event::Log` emitted to the page
  as the `state` and `log` events, `Event::Exit` ending the application;
- the `dialog` and `opener` plugins, which only the page calls;
- with `close_through_engine`, a window close and the macOS Quit item
  (Cmd-Q) become the engine's `quit` command, and the window closes when the
  engine sends `Event::Exit`. On macOS the shell installs Tauri's default
  menu with its own Quit item, id `quit`, because the default Quit sends
  Cocoa's `terminate:`, which ends the process with no close request to
  intercept. The Dock's Quit and a log-out still end the process directly;
- with `keep_awake`, the process opts out of macOS App Nap, so an attached
  tag's 400 ms status poll keeps running while the window is in the
  background;
- the user manual's page for the application, found beside the executable,
  in a build tree or the source tree, else the published manual
  (`user_guide_url`).

The build-script half, which stages the page and writes the shared
capabilities, is [`tagapp-shell-build`](../tagapp-shell-build-rs/README.md).

## Use

```rust
use tagapp_shell::App;
use tagmonitor_core::engine::{self, EngineConfig};

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
            user_guide_url: Some(setup.user_guide_url()),
            auto_attach: std::env::var_os("TAGMONITOR_NO_ATTACH").is_none(),
        };
        Ok(engine::spawn(config, setup.sink()))
    });
}
```

| Item | What it is |
| --- | --- |
| `App` | The application's name, its manual page, and the two switches `keep_awake` and `close_through_engine` |
| `run(app, context, start)` | Installs the toolkit's logger, builds and runs Tauri; `start` is called once in Tauri's setup and returns the `tagapp_engine::Engine`, which the shell keeps as managed state. Panics with "error while running <name>" if Tauri or `start` fails |
| `Setup` | What `start` gets: the running `tauri::App` (field `app`), and two methods |
| `Setup::user_guide_url()` | The manual page's URL, for the engine's Help |
| `Setup::sink()` | The event sink to spawn the engine with (`impl Fn(Event) + Send + Sync + 'static`): emits `State` and `Log` to the page, ends the application on `Exit` |
| `user_guide_url(page)` | The manual page's `file://` URL, or the published one |

The shell depends on `tagapp-engine`, never the reverse: every core's
handle is the one concrete type `tagapp_engine::Engine`, so the shell names
it and needs no trait or adapter, and the toolkit and the cores have no
Tauri dependency and build and test without a window. An application's
crate therefore needs no `log` dependency of its own.

| Application | `keep_awake` | `close_through_engine` |
| --- | --- | --- |
| Tag Programmer | yes | yes |
| Tag Monitor | yes | yes |
| Tag Calibrator | yes | yes |
| Tag View | no | no |

## Requirements on an application

- **Depend on `tauri` directly.** `generate_context!` expands in the
  application's crate, reads its `tauri.conf.json`, and names `tauri`.
- **Depend on `tauri-plugin-dialog` and `tauri-plugin-opener` directly**,
  although this crate registers them: Cargo passes a plugin's permission
  metadata only to its direct dependents' build scripts, and the shared
  capabilities name both plugins.
- **Use `tagapp-shell-build` as the build script**, so the page is staged
  and the capabilities written.
- **With `close_through_engine`, the engine must handle `quit`** and
  eventually send `Event::Exit` (the toolkit's `take_exit`); otherwise the
  window cannot be closed.
- Keep the `#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]`
  in `main.rs`: it describes the executable, so it cannot live here.

## Building and checking

The crate builds with the host workspace; it has no tests of its own, since
everything it does needs a window. It is checked through the applications
(the bench checks for each: launch, title, user guide, Cmd-W, Cmd-Q, reload,
and, where they apply, attach and detach and quitting during programming).

```sh
cd host
cargo build -p tagapp-shell
cargo clippy -p tagapp-shell --all-targets
RUSTDOCFLAGS="-D warnings" cargo doc --no-deps --document-private-items -p tagapp-shell
```

The design is in chapter 10 of the
[Host Software Guide](../../design/guide/10-application-pattern.md#the-tauri-shell),
read line by line in chapter 10 of the
[tutorial](../../design/tutorial/10-tauri-shell.md). Why there is one shell:
[the proposal](../../design/proposals/unified-app-shell.md).
