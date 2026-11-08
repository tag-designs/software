//! Tag Monitor's model and engine, with no UI toolkit: the Rust port of
//! qtmonitor's Qt-free core (`host/applications/qtmon/core`).
//!
//! The [`model`] holds every rule the window applies: which commands are
//! available, the start checks, small tag rules, the monitor model that
//! interprets each status, the configuration document, and the menus as
//! data. Each module names the C++ file it ports. The C++ core's
//! language-neutral oracles check the port: `tests/oracles.rs` compares the
//! command and start-check tables with the files in
//! `host/applications/qtmon/test/`, and `tests/core_check.rs` repeats
//! `qtmon_core_check`'s assertions and compares the menu layout with
//! `test/menus.txt`.

pub mod engine;
pub mod model;
