//! Runs the engine without a UI and prints the view state: the first-run
//! prompt, then the checks with automatic attach off, so the tag is never
//! attached (the base is listed and identified only). Files go under the
//! directory given as the first argument.
//!
//! `cargo run -p tagprogram-core --example smoke -- /tmp/tagprogram-smoke`

use std::time::Duration;

use serde_json::json;
use tagprogram_core::engine::{self, EngineConfig, Event};

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
