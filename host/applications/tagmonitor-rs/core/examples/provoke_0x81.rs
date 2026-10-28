//! `provoke_0x81`: provokes the base's 0x81 (STLINK_DEBUG_ERR_FAULT) bursts
//! on an L4 tag and counts them, for `embedded/bases/TODO.md`.
//!
//! Talks to the tag with tagcore directly, not through the engine, because the
//! engine pauses its polls after exactly the commands that set the bursts off.
//! Attaches through the base with `--serial` (or the only base), waits for a
//! definite state, then `--rounds` times: Synchronize Clock and poll status
//! every `--poll-ms` for 3 s; Self-Test and poll for 8 s. Every tagcore log
//! line is printed with the host's ms since start, to line up with the base's
//! own log (`bittag-base-v7` with fault logging, on its USB serial port).
//! Detaches at the end. It does not start, stop or erase the tag; the attach
//! resets it, and Synchronize Clock sets its clock.
//!
//! `--dbg-sleep` sets `DBGMCU_CR.DBG_SLEEP` (STM32L4) after attaching, which
//! keeps HCLK running while the tag sleeps in WFI, so debug accesses are not
//! held off; the tag's Stop and terminal-sleep paths clear it again.
//!
//! ```sh
//! cargo run -p tagmonitor-core --example provoke_0x81 -- --serial 8009001A5556431520303537
//! ```

use std::process::ExitCode;
use std::sync::OnceLock;
use std::thread::sleep;
use std::time::{Duration, Instant};

use tagcore::Tag;
use tagcore::proto::{TagState, TestReq};

/// When the program started, for the log stamps.
static START: OnceLock<Instant> = OnceLock::new();

/// Milliseconds since start.
fn ms() -> u128 {
    START.get_or_init(Instant::now).elapsed().as_millis()
}

/// Prints tagcore's log records at WARN and above with the host time.
struct StampLogger;

impl log::Log for StampLogger {
    /// WARN and ERROR only: the failures.
    fn enabled(&self, m: &log::Metadata<'_>) -> bool {
        m.level() <= log::Level::Warn
    }
    /// One line, stamped.
    fn log(&self, r: &log::Record<'_>) {
        if self.enabled(r.metadata()) {
            println!("{:>7} {} {}", ms(), r.level(), r.args());
        }
    }
    /// Nothing buffered.
    fn flush(&self) {}
}

/// The logger.
static LOGGER: StampLogger = StampLogger;

/// Polls status every `poll` for `span`; returns (reads, failures).
fn poll_for(tag: &Tag, span: Duration, poll: Duration) -> (u32, u32) {
    let end = Instant::now() + span;
    let (mut reads, mut failures) = (0, 0);
    while Instant::now() < end {
        reads += 1;
        if tag.status().is_err() {
            failures += 1;
        }
        sleep(poll);
    }
    (reads, failures)
}

/// Parses the options and runs the rounds.
fn main() -> ExitCode {
    let _ = log::set_logger(&LOGGER);
    log::set_max_level(log::LevelFilter::Warn);
    ms();
    let (mut serial, mut rounds, mut poll_ms) = (String::new(), 3u32, 100u64);
    let mut dbg_sleep = false;
    let mut args = std::env::args().skip(1);
    while let Some(a) = args.next() {
        match a.as_str() {
            "--serial" => serial = args.next().unwrap_or_default(),
            "--rounds" => rounds = args.next().and_then(|v| v.parse().ok()).unwrap_or(3),
            "--poll-ms" => poll_ms = args.next().and_then(|v| v.parse().ok()).unwrap_or(100),
            "--dbg-sleep" => dbg_sleep = true,
            _ => {
                eprintln!(
                    "usage: provoke_0x81 [--serial SN] [--rounds N] [--poll-ms MS] [--dbg-sleep]"
                );
                return ExitCode::from(2);
            }
        }
    }
    let poll = Duration::from_millis(poll_ms);
    let tag = Tag::new();
    let bases = tag.available();
    let base = match bases
        .iter()
        .find(|b| serial.is_empty() || b.serial == serial)
    {
        Some(b) if serial.is_empty() && bases.len() > 1 => {
            eprintln!(
                "several bases; pass --serial ({})",
                bases
                    .iter()
                    .map(|b| b.serial.as_str())
                    .collect::<Vec<_>>()
                    .join(", ")
            );
            let _ = b;
            return ExitCode::from(2);
        }
        Some(b) => b.clone(),
        None => {
            eprintln!("no base with serial {serial}");
            return ExitCode::FAILURE;
        }
    };
    println!("{:>7} attach base {}", ms(), base.serial);
    if !tag.attach(&base) {
        eprintln!("attach failed");
        return ExitCode::FAILURE;
    }
    if dbg_sleep {
        // DBGMCU_CR on the STM32L4; bit 0 is DBG_SLEEP.
        const DBGMCU_CR: u32 = 0xE004_2004;
        let mut was = [0u8; 4];
        let _ = tag.read_memory(DBGMCU_CR, &mut was);
        let set = u32::from_le_bytes(was) | 1;
        let r = tag.write_memory(DBGMCU_CR, &set.to_le_bytes(), false);
        let mut now = [0u8; 4];
        let _ = tag.read_memory(DBGMCU_CR, &mut now);
        println!(
            "{:>7} DBGMCU_CR 0x{:08X} -> 0x{:08X} ({r:?})",
            ms(),
            u32::from_le_bytes(was),
            u32::from_le_bytes(now)
        );
    }
    let info = tag.tag_info().unwrap_or_default();
    println!(
        "{:>7} {} {} fw {}, base supply {:?} V",
        ms(),
        info.tag_type().as_str_name(),
        info.uuid,
        info.githash,
        tag.voltage()
    );
    let mut state = TagState::StateUnspecified;
    for _ in 0..20 {
        if let Ok(s) = tag.status() {
            state = s.state();
            if state != TagState::StateUnspecified {
                break;
            }
        }
        sleep(Duration::from_millis(500));
    }
    println!("{:>7} state {}", ms(), state.as_str_name());
    if state != TagState::Idle {
        eprintln!("the tag is {}, not IDLE; stopping", state.as_str_name());
        tag.detach();
        return ExitCode::FAILURE;
    }
    let (mut total_reads, mut total_failures) = (0, 0);
    for round in 1..=rounds {
        let sync = tag.set_rtc().is_ok();
        println!(
            "{:>7} round {round}: sync {}",
            ms(),
            if sync { "accepted" } else { "refused" }
        );
        let (r, f) = poll_for(&tag, Duration::from_secs(3), poll);
        println!(
            "{:>7} round {round}: after sync {f} of {r} status reads failed",
            ms()
        );
        total_reads += r;
        total_failures += f;
        let test = tag.test(TestReq::RunAll).is_ok();
        println!(
            "{:>7} round {round}: self-test {}",
            ms(),
            if test { "accepted" } else { "refused" }
        );
        let (r, f) = poll_for(&tag, Duration::from_secs(8), poll);
        let result = tag
            .status()
            .map(|s| s.test_status().as_str_name())
            .unwrap_or("?");
        println!(
            "{:>7} round {round}: after self-test {f} of {r} status reads failed; test {result}",
            ms()
        );
        total_reads += r;
        total_failures += f;
    }
    tag.detach();
    println!(
        "{:>7} detached: {total_failures} of {total_reads} status reads failed",
        ms()
    );
    ExitCode::SUCCESS
}
