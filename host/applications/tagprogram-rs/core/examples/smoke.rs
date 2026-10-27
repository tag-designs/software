//! Runs the engine without a UI and prints the view state: the first-run
//! prompt, then the checks with automatic attach off, so the tag is never
//! attached (the base is listed and identified only). Files go under the
//! directory given as the first argument.
//!
//! `cargo run -p tagprogram-core --example smoke -- /tmp/tagprogram-smoke`
//!
//! It exercises the whole engine path a UI uses -- [`engine::spawn`], the
//! `start` command, a prompt answered by command, and [`Event::State`] --
//! without the Tauri shell. Identifying the base sends it the `F6 99`
//! command but does not touch the tag. The first-run prompt only appears
//! with a fresh scratch directory; reusing one prints `prompt: null`, and
//! if an earlier run saved automatic attach on (as `bench` does), the
//! station attaches, resetting the tag. It prints the state after a fixed
//! 8 s, whatever has finished by then.

use std::time::Duration;

use serde_json::json;
use tagprogram_core::engine::{self, EngineConfig, Event};

/// Starts the engine on the scratch directory, sends `start`, prints the
/// prompt, answers it with automatic attach off, and prints the view state
/// 8 s later.
///
/// # Panics
///
/// Panics without a scratch-directory argument.
fn main() {
    let dir = std::path::PathBuf::from(std::env::args().nth(1).expect("a scratch directory"));
    let config = EngineConfig {
        cache_dir: dir.join("firmware"),
        settings_path: dir.join("settings.json"),
        documents_dir: dir.join("Documents"),
        user_guide_url: None,
    };
    let _ = log::set_logger(&engine::LOGGER);
    let latest = std::sync::Arc::new(std::sync::Mutex::new(serde_json::Value::Null));
    let keep = latest.clone();
    let engine = engine::spawn(config, move |event| match event {
        Event::State(state) => *keep.lock().unwrap() = state,
        Event::Log { level, text } => eprintln!("{level} {text}"),
    });
    engine.command("start", json!({}));
    std::thread::sleep(Duration::from_millis(500));
    println!("prompt: {}", latest.lock().unwrap()["prompt"]);
    engine.command("answer_auto_attach", json!({ "on": false }));
    std::thread::sleep(Duration::from_secs(8));
    let state = latest.lock().unwrap().clone();
    println!("{}", serde_json::to_string_pretty(&state).unwrap());
}
