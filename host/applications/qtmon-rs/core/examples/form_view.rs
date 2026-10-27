//! Prints the Configuration form's view (the state's `config.panels`) for a
//! tag type's default configuration, as JSON: the sample `ui/mock-engine.js`
//! shows.
//!
//! ```sh
//! cargo run -p qtmon-core --example form_view -- BITTAG
//! ```

use qtmon_core::model::configeditor::ConfigEditor;
use tagcore::proto::TagType;

/// Prints the view for the tag type named on the command line (BITTAG by
/// default) at a fixed clock, so the output does not change from run to run.
fn main() {
    let name = std::env::args().nth(1).unwrap_or_else(|| "BITTAG".into());
    let Some(tag_type) = TagType::from_str_name(&name) else {
        eprintln!("unknown tag type {name}");
        std::process::exit(2);
    };
    let mut editor = ConfigEditor::new();
    // 2026-10-08 12:22:03 UTC, the probe cases' clock.
    let now = 1_791_460_923;
    if editor.load_defaults(tag_type, now).is_none() {
        eprintln!("{name} has no default configuration");
        std::process::exit(1);
    }
    println!(
        "{}",
        serde_json::to_string_pretty(&editor.form().view()).expect("JSON")
    );
}
