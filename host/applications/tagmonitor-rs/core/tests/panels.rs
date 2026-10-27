//! The panel table against every tag type's default configuration: the
//! Rust counterpart of `qtmonitor --check-config-panels`.
//!
//! Fails when a tag type has a `default-config.json` but no panels, panels
//! but no default configuration, a panel editing a field the tag does not
//! have, or a field no panel edits. See `design/adding-a-tag-type.md`.

use tagmonitor_core::model::configform::check_panels_against_shapes;

/// Every tag type's panels agree with its default configuration.
#[test]
fn panels_agree_with_every_default_configuration() {
    let problems = check_panels_against_shapes();
    assert!(
        problems.is_empty(),
        "panel table problems:\n{}",
        problems.join("\n")
    );
}
