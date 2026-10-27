---
type: readme
status: current
summary: Tag Monitor in Rust, the port of qtmonitor in progress - what is ported so far, how it is checked against the C++ app's oracles, and how to build, run and test it.
---

# Tag Monitor (Rust)

A port of [qtmonitor](../qtmon/README.md) to Rust, built the way
[Tag Programmer](../tagprogram-rs/README.md) was:

- a toolkit-free `core/` crate with the model and an engine;
- a generic web UI on [`host/ui-kit`](../../ui-kit/README.md);
- a thin Tauri shell.

It follows the structure qtmonitor was given for this purpose
([architecture](../qtmon/design/architecture.md),
[decision 0036](../../../docs/decisions/0036-host-qtmonitor-engine-model-view.md)).

**Status: in progress, on the `qtmon-rs` branch.**

| Phase | What | State |
| --- | --- | --- |
| 1 | Model: the port of `qtmon/core` | done |
| 2 | Engine on tagcore-rs, view state, Tauri shell, UI | done; not yet tried on a tag |
| 3 | Configuration tab: the panels' rules as a field spec | not started |
| 4 | Design docs, CMake and packaging | not started |

## The model

`core/src/model/` ports `host/applications/qtmon/core/` module for module:

| Rust | C++ | Holds |
| --- | --- | --- |
| `commands.rs` | `commands.h` | The commands and the one rule for when each is available |
| `startchecks.rs` | `startchecks.h` | The start checks and their sentences |
| `tagrules.rs` | `tagrules.h` | Downloadable states, erase size, clock offset, version floor, base names |
| `monitormodel.rs` | `monitormodel.h` | The attached tag as known, and what each status settles |
| `configdocument.rs` | `configdocument.h` | The configuration document, its file format and approval |
| `menus.rs` | `menus.h` | The menus as data |

## The engine, shell and page

`core/src/engine/` is qtmonitor's `MainWindow` and `LiveTagSession` without
Qt: a tag thread that owns the `Tag`, and an engine thread that owns the
model, the document and the view state. `app/` is the Tauri shell and `ui/`
the page. The protocol between them, and where this port differs from
qtmonitor, is [design/ui-engine-contract.md](design/ui-engine-contract.md).

## Building and running

```sh
cargo run -p qtmon                    # the app; attaches to a tag at once
QTMON_NO_ATTACH=1 cargo run -p qtmon  # the app, without attaching
open ui/index.html                    # the page alone, against ui/mock-engine.js
```

Download Data... writes SQLite through the C++ writer, so it is built only
with the `sqlitelog` feature and `libtagcore_c` from a CMake build
(`-DBUILD_TAGCORE_C=ON`; see
[taglogwriter-rs](../../libraries/taglogwriter-rs/README.md)):

```sh
TAGCORE_C_LIB_DIR=<build-dir>/Release/lib cargo run -p qtmon --features sqlitelog
```

## Checking

```sh
cargo test
```

- **`tests/oracles.rs`** checks that the command rule and the start checks
  print exactly qtmonitor's `test/action-states.txt` and
  `test/start-checks.txt`.
- **`tests/core_check.rs`** repeats `qtmon_core_check`'s assertions: the
  document, the monitor model, and the window's remaining rules. It also
  compares the menu bar with the menu-bar part of `test/menus.txt`.

These are the same files that hold qtmonitor fixed, so the two
implementations cannot drift apart without a test failing.
