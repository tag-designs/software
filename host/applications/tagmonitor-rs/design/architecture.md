---
type: design
status: current
summary: How Tag Monitor in Rust is put together - the three layers and two threads, a roadmap of every file and the qtmonitor code it ports, how a command and a form edit travel through the code, where to make common changes, and what keeps it in step with qtmonitor.
---

# Tag Monitor (Rust): architecture and source map

Tag Monitor in Rust (`tagmonitor`) is qtmonitor's behaviour in three layers:

- **`core/`**, the crate `tagmonitor-core`: every rule and all state, with no
  UI toolkit. `src/model/` is pure logic; `src/engine/` runs it on two threads
  and talks to the tag.
- **`ui/`**: a web page that draws the state the engine publishes and sends
  commands. It holds no rules.
- **`app/`**, the crate `tagmonitor`: the Tauri shell that runs the engine and
  connects it to the page.

The protocol between engine and page is the
[UI/engine contract](ui-engine-contract.md); building and shipping is
[build and packaging](build-and-packaging.md). To add a tag type, follow
[adding a tag type](adding-a-tag-type.md). Why it was built this way is
[decision 0037](../../../../docs/decisions/0037-host-tagmonitor-rs.md).

## The shape of it

```text
 page (ui/)                      engine thread (engine/monitor.rs)             tag thread (engine/tagthread.rs)
 ─────────                       ───────────────────────────────               ───────────────────────────────
 click ── command ─────────────▶ Monitor::command ── TagCmd ─────────────────▶ runs it on the Tag
                                   uses model/ (rules, document, form)          polls status every 400 ms
 draws ◀── state (whole JSON) ─── view.rs builds the view state   ◀── Report ── statuses, results, downloads
 dialog ◀── state.prompt                                                         (download.rs: C++ writer)
 answer ── command "answer" ───▶ continues what the prompt interrupted
```

- **One owner per thing.** The tag thread is the only code that touches the
  `tagcore::Tag`. The engine thread owns everything else: the
  `MonitorModel`, the `ConfigEditor` (document and form), the queued prompts
  and the progress displays. Nothing is shared between them; they exchange
  messages (`Msg`, `TagCmd`, `Report`).
- **The engine never waits on the tag**, so the window stays live during a
  download, and a slow or silent tag shows as "no answer" rather than a hung
  window.
- **The page draws, nothing more.** Every text, every enabled state and every
  dialog comes from the engine. The page's only state is which tab is shown,
  the log text and the log level.

## Source map

Each Rust file names the qtmonitor file it ports in its first lines. Paths
below are relative to `host/applications/tagmonitor-rs`; qtmonitor's are
relative to `host/applications/qtmon`.

### `core/src/model/`: the rules

Pure logic: no threads, no tag, no I/O except reading the embedded default
configurations. Everything here is unit-testable and checked against
qtmonitor's oracles.

| File | Holds | Ports |
| --- | --- | --- |
| `commands.rs` | The twelve commands and the one rule (`availability`) for when each is enabled; the action-state table | `core/commands.h` |
| `menus.rs` | The menu bar as data: each entry's label, tooltip and shortcut, and the order | `core/menus.h` |
| `startchecks.rs` | The checks Start makes (clock, self-test, approval) and their sentences | `core/startchecks.h` |
| `tagrules.rs` | Small tag rules: downloadable states, erase progress size, clock offset, version floor, how a base is listed (plus its identity, which qtmonitor lacks) | `core/tagrules.h` |
| `monitormodel.rs` | What is known about the attached tag, and what each status settles: a self-test's result, a start's success, an erase's end, the tag's debug text as lines | `core/monitormodel.h` |
| `configdocument.rs` | The configuration document: its values, source (defaults, file, tag), unsaved-changes baseline, approval, and the file format | `core/configdocument.h` |
| `configform.rs` | The Configuration tab's panels, the Qt widget behaviour they rely on, the panel table, and the panel-table check | `tagconfiguration/*panel*`, `hibernate.*`, `hourtime.h`, `configtab.cpp` (table) |
| `configeditor.rs` | The document and the form together: load with baseline, New, Open, Reset, what Start sends, when the form is editable | `tagconfiguration/configtab.cpp` (logic) |

`configform.rs` is the largest file and the one most likely to need work:

- **Widgets** (`DateEdit`, `DoubleSpin`, `IntSpin`, `RadioGroup`, `qt_round`):
  small models of `QDateTimeEdit`, `QDoubleSpinBox`, `QSpinBox` and
  `QButtonGroup`. What Start sends depends on their clamping and rounding,
  so they are reproduced, not approximated.
- **Panels** (`Schedule` with `HibernateRow`, `Adxl`, the BitTag log and LSM6
  radio groups, gathered in `Panel`): each has `load`, `store`, `controls`
  (what the page and the probe see), `edit` (one user change) and `paths`
  (the Config fields it owns).
- **The form** (`ConfigForm`): every panel built once, `panels_for_tag`
  choosing which are shown, and `fingerprint`, the probe's description of
  every control.

### `core/src/engine/`: the threads

| File | Holds | Ports |
| --- | --- | --- |
| `mod.rs` | `Engine` (the handle the shell uses), `Msg`, `Event`, `spawn`, the engine loop, and `UiLogger` (log records to the page) | `session/livetagsession.*`, `mainwindow.cpp` (wiring) |
| `tagthread.rs` | The tag thread: `TagCmd` in, `Report` out, the status poll, the quiet period after Sync and Self-Test, base identification | `session/livetagsession.cpp` (`TagWorker`) |
| `monitor.rs` | `Monitor`, the engine thread's state: commands, prompts and their answers (`After` says what continues), reports from the tag thread, the recent-files list | `mainwindow.cpp` (commands and session reports), `configtab.cpp` (document commands) |
| `view.rs` | The view state JSON: status strip, Tag Info page, form, actions, prompt, progress; and the `menus` query | `mainwindow.cpp` (`render`), `tagstatusstrip.cpp` |
| `download.rs` | Download Data...: the page loop over the C++ writer, behind the `sqlitelog` feature | `core/downloadjob.cc` |

### `core/tests/` and `core/examples/`: the checks

| File | Checks |
| --- | --- |
| `tests/oracles.rs` | The command table and start checks print exactly qtmonitor's `test/action-states.txt` and `test/start-checks.txt` |
| `tests/core_check.rs` | `qtmon_core_check`'s assertions, and the menu bar against `test/menus.txt` |
| `tests/config_probe.rs` | All 710 configuration probe cases (`test/cases/`) against the goldens (`test/golden/`): bytes sent, document, panels, dialogs, every control |
| `tests/panels.rs` | The panel table against every tag type's default configuration (qtmonitor's `--check-config-panels`) |
| unit tests in `src/` | Edit rules the probe cannot reach, rounding, hour arithmetic, base labels, clock text, download names |
| `examples/session_check.rs` | A whole tag cycle through the engine, on hardware; changes the tag (see its header) |
| `examples/form_view.rs` | Prints one tag type's form as the page receives it; the browser mock's sample |

### `app/`: the shell

| File | Holds |
| --- | --- |
| `src/main.rs` | Starts the engine; the `command` and `query` Tauri commands; forwards `state` and `log` events; turns a window close into `quit` and exits on the engine's `Exit`; finds the user guide; opts out of App Nap |
| `build.rs` | Stages `ui/` and `host/ui-kit/` for Tauri; the rpath to a shared `tagcore_c` |
| `tauri.conf.json`, `capabilities/default.json` | The window, the identifier, the permissions (dialogs, opener, window title) |
| `Cargo.toml` | The `sqlitelog` feature, passed to the core |

### `ui/`: the page

| File | Holds |
| --- | --- |
| `index.html` | The window: menu bar, tabs (Configuration, Tag Info, Error Log), status strip, pages |
| `app.js` | Draws the state; builds the menus and popup menu from the `menus` query; the generic form renderer; shows prompts and sends answers; the progress sheets; keyboard shortcuts |
| `app.css` | Layout only; the look is the [UI kit](../../../ui-kit/README.md)'s |
| `mock-engine.js` | A stand-in engine for designing in a browser, with scenarios (`?scenario=`) |

### The rest

| Path | Holds |
| --- | --- |
| `CMakeLists.txt`, `cmake/Info.plist.in` | The CMake target and per-platform install ([build and packaging](build-and-packaging.md)) |
| `Cargo.toml`, `Cargo.lock` | The workspace; the lock file is committed and release builds use it |
| `design/` | This page, the contract, packaging, adding a tag type |

## How a command travels: Start

1. The page's Start button (or Tag ▸ Start) sends `command("start")`.
2. `Monitor::command` finds the command in `ALL_COMMANDS` and runs it only if
   `availability` (`commands.rs`) enables it now.
3. `start_with_checks` asks `startchecks.rs` which checks fail. If any do, it
   queues a `StartAnyway` prompt and stops there.
4. The page shows the prompt (once, by id) and answers
   `answer {id, button: "start"}`; `Monitor::answer` pops the prompt and calls
   `start`.
5. `start` asks `ConfigEditor::get_config` for what the form would send and
   sends `TagCmd::Start` to the tag thread.
6. The tag thread calls `Tag::start` and reports `Report::StartSent` (with the
   tag's message if the call failed), then reads a status.
7. `Monitor::report` records the start in the `MonitorModel`. Each later
   status goes through `MonitorModel::apply_status`, which settles the start
   once the tag reaches RUNNING or CONFIGURED (`StartSucceeded`), or reports
   `StartFailed` after 7 s, which the monitor turns into an alert.
8. After each message the loop rebuilds the view state (`view.rs`) and sends
   it if it changed; the page redraws.

A prompt that needs more than one answer carries its continuation:
`Unsaved(After)` runs the `After` once the user saves or discards, so Open,
Read, New and Quit all pass through the same unsaved-changes question.

## How a form edit travels

1. The page sends `set_field {id: "adxl.range", value: 1}`.
2. `ConfigEditor::edit` passes it to `ConfigForm::edit`, which offers it to
   each shown panel; `Adxl::edit` checks the button and runs `range_clicked`,
   which re-limits both thresholds (`DoubleSpin::set_range` clamps them), as
   the Qt panel's slot does.
3. The edit withdraws approval; `is_modified` compares what the form would
   send now with what it sent just after loading.
4. The next view state carries the clamped values; the page shows them.

## Where to change what

| To change | Edit | Then check |
| --- | --- | --- |
| When a command is enabled | `model/commands.rs`, and qtmonitor's `core/commands.cc` | `tests/oracles.rs`; update `qtmon/test/action-states.txt` with qtmonitor's output |
| A menu label, tooltip or order | `model/menus.rs`, and `core/menus.cc` | `tests/core_check.rs` against `test/menus.txt` |
| A start check | `model/startchecks.rs`, and `core/startchecks.cc` | `tests/oracles.rs` |
| What a status means (test, start, erase) | `model/monitormodel.rs`, and `core/monitormodel.cc` | `tests/core_check.rs` |
| A configuration panel's rules | `model/configform.rs`, and the Qt panel | regenerate the probe goldens from qtmonitor, then `tests/config_probe.rs` |
| A tag type | see [adding a tag type](adding-a-tag-type.md) | `tests/panels.rs`, `tests/config_probe.rs` |
| A dialog or what follows it | `PromptKind` and `Monitor::answer` (`engine/monitor.rs`), its JSON in `engine/view.rs`, and `showPrompt` in `ui/app.js`; document it in the contract | run the app, or the mock |
| Something the window shows | `engine/view.rs`, then `ui/app.js`; keep `ui/mock-engine.js` and the contract in step | the mock in a browser |
| The status poll or quiet periods | `engine/tagthread.rs` | `examples/session_check.rs` on a tag |
| Download | `engine/download.rs`; the file format is the C++ writer's | `examples/session_check.rs` |

## Keeping in step with qtmonitor

Two Tag Monitors ship. They are held to the same behaviour by qtmonitor's own
test files, which the Rust tests read in place (`host/applications/qtmon/test/`):
`action-states.txt`, `start-checks.txt`, `menus.txt`, the probe `cases/` and
`golden/`. A change to qtmonitor's behaviour updates those files in the same
commit, and the Rust port then fails until it is changed to match. Deliberate
differences are listed in the [contract](ui-engine-contract.md#deliberate-differences-from-qtmonitor).

## Checking a change

```sh
cd host/applications/tagmonitor-rs
cargo test                                   # every check above but the hardware one
cargo clippy --workspace --all-targets       # no warnings, documentation included
RUSTDOCFLAGS="-D warnings" cargo doc --no-deps --document-private-items
open ui/index.html                           # the page against the mock
```
