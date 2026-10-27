//! qtprogram's `--bench-program`, on the engine: attach, program one tag with
//! a release, print each step, and report the record row.
//!
//! `cargo run -p tagprogram-core --example bench -- <scratch-dir> <release> <label> [--retest] [--debug]`
//!
//! Settings and the record file go under `<scratch-dir>`; downloaded
//! releases go in `<scratch-dir>/firmware`. Attaching resets the tag. Exit
//! status: 0 for a written OK row, 1 otherwise, 2 if it cannot program.
//!
//! It drives the engine only as a UI would, through commands, queries and
//! the published view state: it answers the first-run prompt with automatic
//! attach **on** (so the station attaches as soon as it finds the base),
//! creates or opens `<scratch-dir>/programmed-tags.csv`, chooses the
//! release, waits up to 180 s for the download and attach to settle, then
//! sends `program` (after the `program_preview` query) or `retest`. While
//! the session waits for a power cycle it presses Retry every 5 s, and
//! gives up after 36 tries. `--debug` sets the log level to DEBUG, which
//! adds the session's per-tick lines. Nothing confirms a label conflict:
//! the label is used as given.
//!
//! This is a hardware procedure: follow `docs/bench/` (supply voltage, board
//! UUID, erase before flashing) before pointing it at a tag.

use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use serde_json::{Value, json};
use tagprogram_core::engine::{self, EngineConfig, Event};

/// Polls the latest view state every 200 ms until `done` accepts it or
/// `timeout` passes, and returns the last state seen either way.
fn wait_until(latest: &Mutex<Value>, timeout: Duration, done: impl Fn(&Value) -> bool) -> Value {
    let end = Instant::now() + timeout;
    loop {
        let state = latest.lock().unwrap().clone();
        if done(&state) || Instant::now() > end {
            return state;
        }
        std::thread::sleep(Duration::from_millis(200));
    }
}

/// Prints the eight checks, one per line: name, level (`FAIL` and `-` for
/// fail and pending) and text.
fn print_checks(state: &Value) {
    for c in state["checks"].as_array().into_iter().flatten() {
        let level = match c["level"].as_str().unwrap_or("") {
            "fail" => "FAIL",
            "pending" => "-",
            l => l,
        };
        println!(
            "{:<10} {:<5} {}",
            c["name"].as_str().unwrap_or(""),
            level,
            c["text"].as_str().unwrap_or("")
        );
    }
}

/// The station has settled after start-up: a state has arrived and none of
/// the SWD, Firmware and Tag checks is pending (the download and attach are
/// done, or failed).
fn settled(state: &Value) -> bool {
    let checks = state["checks"].as_array();
    let pending = |name: &str| {
        checks
            .into_iter()
            .flatten()
            .any(|c| c["name"] == name && c["level"] == "pending")
    };
    !state.is_null() && !pending("SWD") && !pending("Firmware") && !pending("Tag")
}

/// Runs one bench program or retest and exits with its status (see the
/// module documentation).
///
/// # Panics
///
/// Panics if the view-state mutex is poisoned (the sink panicked).
fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 4 {
        eprintln!("usage: bench <scratch-dir> <release> <label> [--retest] [--debug]");
        std::process::exit(2);
    }
    let dir = std::path::PathBuf::from(&args[1]);
    let retest = args.iter().any(|a| a == "--retest");
    let level = if args.iter().any(|a| a == "--debug") {
        "DEBUG"
    } else {
        "INFO"
    };
    let config = EngineConfig {
        cache_dir: dir.join("firmware"),
        settings_path: dir.join("settings.json"),
        documents_dir: dir.join("Documents"),
        user_guide_url: None,
    };
    let _ = log::set_logger(&engine::LOGGER);
    let latest = Arc::new(Mutex::new(Value::Null));
    let keep = latest.clone();
    let start = Instant::now();
    let engine = engine::spawn(config, move |event| match event {
        Event::State(state) => *keep.lock().unwrap() = state,
        Event::Log { level, text } => {
            let t = start.elapsed().as_secs_f32();
            eprintln!("[{t:7.1}] {level} {text}");
        }
    });
    engine.command("set_log_level", json!({ "level": level }));
    engine.command("start", json!({}));
    let state = wait_until(&latest, Duration::from_secs(5), |s| !s["prompt"].is_null());
    if state["prompt"]["kind"] == "autoAttach" {
        engine.command("answer_auto_attach", json!({ "on": true }));
    }
    let record = dir.join("programmed-tags.csv");
    engine.command("record_create", json!({ "path": record.to_string_lossy() }));
    engine.command(
        "choose_firmware",
        json!({ "id": format!("release:{}", args[2]) }),
    );

    // Downloading a release and attaching both take a while.
    let state = wait_until(&latest, Duration::from_secs(180), settled);
    std::thread::sleep(Duration::from_secs(1));
    let state = latest.lock().unwrap().clone().max_or(state);
    print_checks(&state);

    if retest {
        engine.command("retest", json!({ "label": args[3] }));
    } else {
        let preview = engine.query("program_preview", json!({}));
        if preview["ok"] != true {
            println!(
                "Cannot program: {}",
                preview["reason"].as_str().unwrap_or("?")
            );
            std::process::exit(2);
        }
        println!(
            "Tag:   {}\nImage: {}\nErase: {}",
            preview["tag"], preview["image"], preview["erase"]
        );
        for n in preview["notes"].as_array().into_iter().flatten() {
            println!("Note:  {n}");
        }
        engine.command("program", json!({ "label": args[3] }));
    }

    // Follow the session; at PowerCycle retry every 5 s, 36 times, then give up.
    let mut last = String::new();
    let mut retries = 0;
    let mut last_retry = Instant::now();
    loop {
        std::thread::sleep(Duration::from_millis(200));
        let s = latest.lock().unwrap().clone();
        let text = s["session"]["stepText"].as_str().unwrap_or("").to_owned();
        if text != last {
            println!("[{:7.1}] {text}", start.elapsed().as_secs_f32());
            last = text;
        }
        if s["session"]["retryVisible"] == true && last_retry.elapsed() > Duration::from_secs(5) {
            last_retry = Instant::now();
            retries += 1;
            if retries > 36 {
                engine.command("cancel_session", json!({}));
            } else {
                engine.command("retry_connect", json!({}));
            }
        }
        let result = &s["tag"]["result"];
        if !result["ok"].is_null() {
            println!("Result: {}", result["text"]);
            println!("{}", s["batch"]);
            let rows = std::fs::read_to_string(&record).unwrap_or_default();
            println!("Record:\n{}", rows.trim_end());
            print_checks(&s);
            std::process::exit(if result["ok"] == true { 0 } else { 1 });
        }
    }
}

/// Picks between two view states.
trait MaxOr {
    /// `self`, unless it is null, in which case `other`.
    fn max_or(self, other: Value) -> Value;
}

impl MaxOr for Value {
    /// The newer state, unless it is empty.
    fn max_or(self, other: Value) -> Value {
        if self.is_null() { other } else { self }
    }
}
