//! `session_check`: one whole tag cycle through the engine, with no window.
//!
//! The Rust counterpart of qtmonitor's `qtmon_session_check`
//! (`host/applications/qtmon/test/qtmon_session_check.cpp`), with the same
//! steps and timeouts. It drives the engine only as the page does: commands,
//! answers to prompts, and the published view state. Steps:
//!
//! 1. attach (the first base), wait for a definite state, which must be IDLE;
//! 2. synchronize the clock;
//! 3. self-test, which must give ALL_PASSED;
//! 4. read the configuration, approve it and start with it unchanged;
//! 5. run for `--run-seconds` (default 60);
//! 6. stop;
//! 7. download to `--output` (needs the `sqlitelog` feature);
//! 8. erase;
//! 9. detach.
//!
//! Each step prints its time. Any alert the engine raises fails the run,
//! which then stops, leaving the tag as it is. `--attach-only` stops after
//! step 1 and detaches: it only resets the tag.
//!
//! **It changes the tag:** the attach resets it, then it is started,
//! stopped and erased. Run it only on a bench tag in IDLE with nothing to
//! keep, after the steps in `docs/bench/`.
//!
//! ```sh
//! TAGCORE_C_LIB_DIR=<build>/Release/lib cargo run -p qtmon-core --features sqlitelog \
//!     --example session_check -- --run-seconds 60 --output /tmp/session.db3
//! ```

use std::process::ExitCode;
use std::sync::mpsc::{self, Receiver};
use std::time::{Duration, Instant};

use qtmon_core::engine::{self, Engine, EngineConfig, Event};
use serde_json::{Value, json};

/// The run: the engine, its events, and the latest view state.
struct Run {
    /// The engine under test.
    engine: Engine,
    /// Its events.
    events: Receiver<Event>,
    /// The latest view state.
    state: Value,
}

impl Run {
    /// The strip's `field.text`.
    fn strip(&self, field: &str) -> String {
        self.state["strip"][field]["text"]
            .as_str()
            .unwrap_or("")
            .to_string()
    }

    /// Takes events for up to `wait`; answers prompts with `answer`, which
    /// returns the button and extra arguments, or `Err` to fail the run.
    /// Returns once `done` holds, or `Err` on timeout or a failed prompt.
    fn until(
        &mut self,
        seconds: u64,
        mut answer: impl FnMut(&Value) -> Result<(&'static str, Value), String>,
        mut done: impl FnMut(&Value) -> bool,
    ) -> Result<(), String> {
        let deadline = Instant::now() + Duration::from_secs(seconds);
        let mut answered = None;
        loop {
            if done(&self.state) {
                return Ok(());
            }
            let left = deadline.saturating_duration_since(Instant::now());
            if left.is_zero() {
                return Err("timed out".into());
            }
            match self
                .events
                .recv_timeout(left.min(Duration::from_millis(200)))
            {
                Ok(Event::State(s)) => self.state = s,
                Ok(Event::Log { level, text }) => {
                    if level == "WARN" || level == "ERROR" {
                        eprintln!("  {level} {text}");
                    }
                }
                Ok(Event::Exit) | Err(_) => {}
            }
            let prompt = self.state["prompt"].clone();
            if !prompt.is_null() && answered != prompt["id"].as_u64() {
                answered = prompt["id"].as_u64();
                if prompt["kind"] == "alert" {
                    return Err(format!(
                        "{}: {} {}",
                        prompt["title"].as_str().unwrap_or(""),
                        prompt["text"].as_str().unwrap_or(""),
                        prompt["informative"].as_str().unwrap_or("")
                    ));
                }
                let (button, mut extra) = answer(&prompt)?;
                extra["id"] = prompt["id"].clone();
                extra["button"] = json!(button);
                self.engine.command("answer", extra);
            }
        }
    }

    /// One step: prints its name, sends `cmd`, waits for `done`.
    fn step(
        &mut self,
        name: &str,
        seconds: u64,
        cmd: Option<&str>,
        answer: impl FnMut(&Value) -> Result<(&'static str, Value), String>,
        done: impl FnMut(&Value) -> bool,
    ) -> Result<(), String> {
        print!("{name:<10} ");
        let started = Instant::now();
        if let Some(cmd) = cmd {
            self.engine.command(cmd, json!({}));
        }
        let result = self.until(seconds, answer, done);
        match &result {
            Ok(()) => println!("ok  {:.1} s", started.elapsed().as_secs_f64()),
            Err(why) => println!("FAILED: {why}"),
        }
        result
    }
}

/// No prompt is expected.
fn none(p: &Value) -> Result<(&'static str, Value), String> {
    Err(format!("unexpected prompt {}", p["kind"]))
}

/// Runs the cycle; `Err` names the failure.
fn cycle(run: &mut Run, run_seconds: u64, output: &str, attach_only: bool) -> Result<(), String> {
    let definite = |s: &Value| {
        let st = s["strip"]["state"]["text"].as_str().unwrap_or("");
        s["strip"]["attached"] == true && !st.is_empty() && st != "STATE_UNSPECIFIED"
    };
    run.step(
        "attach",
        20,
        Some("ready"),
        |p| match p["kind"].as_str() {
            Some("chooseBase") => Ok(("ok", json!({ "index": 0 }))),
            // The tag's configuration replaces none, or asks about none.
            Some("unsaved") => Ok(("discard", json!({}))),
            _ => none(p),
        },
        definite,
    )?;
    let info = &run.state["tag"]["info"];
    println!(
        "  {} {}, {}, {}",
        info["tagType"].as_str().unwrap_or(""),
        info["uuid"].as_str().unwrap_or(""),
        run.state["strip"]["voltage"].as_str().unwrap_or(""),
        run.strip("state")
    );
    if attach_only {
        return run.step("detach", 10, Some("detach"), none, |s| {
            s["strip"]["attached"] == false
        });
    }
    if run.strip("state") != "IDLE" {
        return Err(format!("the tag is {}, not IDLE", run.strip("state")));
    }

    run.step("sync", 10, Some("sync"), none, |s| {
        s["strip"]["clock"]["warn"] == false
            && !s["strip"]["clock"]["text"]
                .as_str()
                .unwrap_or("")
                .is_empty()
    })?;
    println!("  clock offset {}", run.strip("clock"));

    let mut seen_running = false;
    run.step("self-test", 70, Some("test"), none, |s| {
        let t = s["strip"]["test"]["text"].as_str().unwrap_or("");
        seen_running |= t == "Running";
        seen_running && t != "Running"
    })?;
    if run.strip("test") != "ALL_PASSED" {
        return Err(format!("self-test is {}", run.strip("test")));
    }
    println!("  ALL_PASSED");

    // Read replaces the document with the tag's; give the read time to land.
    let read_at = Instant::now();
    run.step(
        "read",
        10,
        Some("read"),
        |p| {
            if p["kind"] == "unsaved" {
                Ok(("discard", json!({})))
            } else {
                none(p)
            }
        },
        |s| s["config"]["document"] == true && read_at.elapsed() > Duration::from_secs(1),
    )?;
    println!("  {}", run.state["config"]["name"].as_str().unwrap_or(""));

    run.engine.command("set_approved", json!({ "on": true }));
    run.step("start", 20, Some("start"), none, |s| {
        matches!(
            s["strip"]["state"]["text"].as_str(),
            Some("RUNNING" | "CONFIGURED" | "HIBERNATING")
        )
    })?;
    println!(
        "  {}; running {run_seconds} s without commands",
        run.strip("state")
    );

    let until = Instant::now() + Duration::from_secs(run_seconds);
    run.step("run", run_seconds + 30, None, none, |_| {
        Instant::now() >= until
    })?;
    println!("  {} after the run", run.strip("state"));

    run.step("stop", 40, Some("stop"), none, |s| {
        matches!(
            s["strip"]["state"]["text"].as_str(),
            Some("FINISHED" | "ABORTED")
        )
    })?;
    println!(
        "  {}, {} internal, {} external records",
        run.strip("state"),
        run.state["tag"]["internalCount"].as_str().unwrap_or(""),
        run.state["tag"]["externalCount"].as_str().unwrap_or("-")
    );

    let mut started = false;
    let mut last = (0, 0);
    run.step(
        "download",
        600,
        Some("download"),
        |p| {
            if p["kind"] == "saveFile" {
                Ok(("ok", json!({ "path": output })))
            } else {
                none(p)
            }
        },
        |s| {
            if let Some(d) = s["download"].as_object() {
                started = true;
                last = (
                    d["value"].as_i64().unwrap_or(0),
                    d["max"].as_i64().unwrap_or(0),
                );
            }
            started && s["download"].is_null()
        },
    )?;
    let size = std::fs::metadata(output).map(|m| m.len()).unwrap_or(0);
    println!(
        "  {} of {} records to {output} ({size} bytes)",
        last.0, last.1
    );

    let mut seen_erasing = false;
    run.step(
        "erase",
        300,
        Some("erase"),
        |p| {
            if p["kind"] == "confirmErase" {
                Ok(("ok", json!({})))
            } else {
                none(p)
            }
        },
        |s| {
            let st = s["strip"]["state"]["text"].as_str().unwrap_or("");
            seen_erasing |= st.starts_with("S_RESET") || !s["erase"].is_null();
            seen_erasing && st == "IDLE"
        },
    )?;

    run.step("detach", 10, Some("detach"), none, |s| {
        s["strip"]["attached"] == false
    })
}

/// Parses the options, runs the cycle, and exits 0 when every step passed.
fn main() -> ExitCode {
    let mut run_seconds = 60;
    let mut output = "session.db3".to_string();
    let mut attach_only = false;
    let mut args = std::env::args().skip(1);
    while let Some(a) = args.next() {
        match a.as_str() {
            "--run-seconds" => run_seconds = args.next().and_then(|v| v.parse().ok()).unwrap_or(60),
            "--output" => output = args.next().unwrap_or(output),
            "--attach-only" => attach_only = true,
            _ => {
                eprintln!("usage: session_check [--run-seconds S] [--output PATH] [--attach-only]");
                return ExitCode::from(2);
            }
        }
    }
    let scratch = std::env::temp_dir().join("qtmon-session-check");
    let config = EngineConfig {
        settings_path: scratch.join("settings.json"),
        home_dir: scratch.clone(),
        user_guide_url: None,
        auto_attach: true,
    };
    let _ = log::set_logger(&engine::LOGGER);
    let (tx, events) = mpsc::channel();
    let engine = engine::spawn(config, move |e| {
        let _ = tx.send(e);
    });
    let mut run = Run {
        engine,
        events,
        state: Value::Null,
    };
    match cycle(&mut run, run_seconds, &output, attach_only) {
        Ok(()) => {
            println!("session check: every step passed");
            ExitCode::SUCCESS
        }
        Err(_) => ExitCode::FAILURE,
    }
}
