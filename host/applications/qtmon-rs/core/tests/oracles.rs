//! The C++ qtmonitor core's oracles, applied to the Rust port.
//!
//! The expected files live with the Qt app in
//! `host/applications/qtmon/test/` and are what `qtmonitor --check-*`
//! prints; the port must print exactly the same.

use std::fs;
use std::path::PathBuf;

use qtmon_core::model::commands::action_state_table;
use qtmon_core::model::startchecks::start_check_table;

/// A file in `host/applications/qtmon/test/`.
fn qtmon_test_file(name: &str) -> String {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../qtmon/test")
        .join(name);
    fs::read_to_string(&path).unwrap_or_else(|e| panic!("{}: {e}", path.display()))
}

/// The command rule, against action-states.txt.
#[test]
fn action_states_match_qtmonitor() {
    assert_eq!(action_state_table(), qtmon_test_file("action-states.txt"));
}

/// The start checks, against start-checks.txt.
#[test]
fn start_checks_match_qtmonitor() {
    assert_eq!(start_check_table(), qtmon_test_file("start-checks.txt"));
}
