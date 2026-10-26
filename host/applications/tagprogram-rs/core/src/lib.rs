//! Tag Programmer core: qtprogram's logic with no UI toolkit.
//!
//! `tagprogram-core` is the part of `tagprogram-rs` that owns every rule and
//! all state. The Tauri shell (`app/`) and the web page (`ui/`) only forward
//! commands and draw what they are sent. It has two layers:
//!
//! - [`model`]: pure logic over files -- ELF reading, flash plans, the image
//!   identity record, firmware images, the release catalog, the record CSV
//!   and the settings file. Nothing in it touches USB, so its unit tests run
//!   without hardware.
//! - [`engine`]: qtprogram's `Station` and `ProgramSession`
//!   (`host/applications/qtprogram/station.cpp`, `programsession.cpp`) and
//!   the display rules of its `MainWindow` (`mainwindow.cpp`), run on one
//!   engine thread that talks to the tag through `tagcore` and to the probe
//!   through `tagprobe` (probe-rs).
//!
//! The protocol between the engine and a UI -- the view state, commands,
//! queries and prompts -- is `design/ui-engine-contract.md`; the engine's
//! internals are described in `design/engine.md`. Both paths are relative to
//! `host/applications/tagprogram-rs`.

pub mod engine;
pub mod model;
