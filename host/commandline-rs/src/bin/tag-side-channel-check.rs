//! `tag-side-channel-check`: hardware check for the tagcore memory side
//! channel and keepalive.
//!
//! Port of `host/commandline/tag-side-channel-check.cc`. Attaches to a tag
//! and checks, without changing tag state or writing tag memory:
//! - the target's DBGMCU ID code reads while the tag runs;
//! - a multi-chunk SRAM read succeeds;
//! - unaligned and wrapping ranges return [`MemAccess::InvalidRange`];
//! - on STM32U3, a write over monitor_shared returns
//!   [`MemAccess::Protected`] (the guard refuses it before any write);
//! - status and memory reads from two threads on one [`Tag`] never fail;
//! - `last_error()` is clean after a successful RPC;
//! - with `--idle`, the session survives an idle period with keep-alive
//!   calls, or, with `--no-keepalive`, the control case;
//! - reads after detach return [`MemAccess::NotAttached`].
//!
//! Three opt-in checks change tag memory or exercise failures, and need the
//! operator's agreement first:
//! - `--write-test <addr>` writes two patterns to one SRAM word, reads each
//!   back, then restores and re-reads the original value. The address must
//!   be word aligned, in SRAM and outside monitor_shared on both families;
//!   it is never forced.
//! - `--start-running` sends Start to a tag already RUNNING or HIBERNATING
//!   and expects PERM without a transport failure. It refuses to send Start
//!   to a tag in any other state.
//! - `--unplug <s>` polls status for up to `s` seconds while the operator
//!   unplugs the base, and expects the failure to be reported as a
//!   transport failure. It ends the run.
//!
//! Prints one PASS or FAIL line per check and exits 1 on any FAIL. See
//! `host/libraries/tagcore/TODO.md`. Not for shipping.

use std::cell::Cell;
use std::io::Write as _;
use std::process::ExitCode;
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};
use std::thread::sleep;
use std::time::{Duration, Instant};

use clap::Parser;
use tag_tools::{BaseArgs, init_logging, select_base, state_name};
use tagcore::monitor_abi::{MONITOR_SHARED_ADDR, MONITOR_SHARED_SIZE};
use tagcore::proto::{Status, TagState};
use tagcore::{MemAccess, Tag};

/// check tagcore memory access and keepalive on a live tag
#[derive(Parser)]
#[command(name = "tag-side-channel-check")]
struct Cli {
    #[command(flatten)]
    base: BaseArgs,

    /// idle seconds for the keepalive check (0 skips it)
    #[arg(long, default_value_t = 0)]
    idle: i32,

    /// seconds between KeepAlive calls
    #[arg(long, default_value_t = 2)]
    interval: i32,

    /// control: idle without KeepAlive; the session should end
    #[arg(long)]
    no_keepalive: bool,

    /// seconds for the two-thread check (0 skips it)
    #[arg(long, default_value_t = 10)]
    threads: i32,

    /// write, read back and restore one SRAM word (changes tag memory)
    #[arg(long, value_name = "ADDR")]
    write_test: Option<String>,

    /// send Start to a running tag, expect PERM (asks the tag to start)
    #[arg(long)]
    start_running: bool,

    /// seconds to wait for the base to be unplugged during GetStatus calls
    #[arg(long, default_value_t = 0)]
    unplug: i32,
}

const L4_DBGMCU_IDCODE: u32 = 0xE004_2000;
const U3_DBGMCU_IDCODE: u32 = 0xE004_4000;
const SRAM_BASE: u32 = 0x2000_0000;
/// Upper bound for --write-test addresses: the largest SRAM of the targets
/// (STM32U375, 256 KB). The operator still picks the word from the map file.
const SRAM_LIMIT: u32 = 0x2004_0000;

thread_local! {
    static FAILURES: Cell<i32> = const { Cell::new(0) };
}

/// Prints one PASS or FAIL line and counts failures.
fn report(ok: bool, what: &str, detail: &str) {
    println!(
        "{}  {what}{}{detail}",
        if ok { "PASS" } else { "FAIL" },
        if detail.is_empty() { "" } else { ": " }
    );
    let _ = std::io::stdout().flush();
    if !ok {
        FAILURES.with(|f| f.set(f.get() + 1));
    }
}

fn failures() -> i32 {
    FAILURES.with(Cell::get)
}

/// A plausible STM32 DBGMCU_IDCODE has a nonzero 12-bit device ID.
fn plausible_idcode(v: u32) -> bool {
    v != 0 && v != 0xFFFF_FFFF && (v & 0xFFF) != 0
}

fn hex(v: u32) -> String {
    format!("0x{v:08X}")
}

/// Reads one little-endian word, returning the access result and the word
/// (unchanged from `initial` when the read fails).
fn read_word(tag: &Tag, addr: u32, initial: u32) -> (MemAccess, u32) {
    let mut bytes = initial.to_le_bytes();
    let r = tag.read_memory(addr, &mut bytes);
    (r, u32::from_le_bytes(bytes))
}

/// Polls status until the tag reports a definite state; attach connects
/// under reset, so the first status can be STATE_UNSPECIFIED while the tag
/// boots. `None` on an RPC failure or after about 10 s.
fn settled_status(tag: &Tag) -> Option<Status> {
    for _ in 0..10 {
        let status = tag.status().ok()?;
        if status.state != TagState::StateUnspecified as i32 {
            return Some(status);
        }
        sleep(Duration::from_secs(1));
    }
    None
}

/// Writes patterns to one SRAM word agreed with the operator, reads each
/// back, then restores the original; one PASS or FAIL line per step.
fn write_test(tag: &Tag, addr: u32) {
    if addr & 3 != 0 || !(MONITOR_SHARED_ADDR + MONITOR_SHARED_SIZE..SRAM_LIMIT).contains(&addr) {
        report(
            false,
            "write test",
            &format!(
                "{} is unaligned, inside monitor_shared or outside SRAM; nothing written",
                hex(addr)
            ),
        );
        return;
    }
    let (r, original) = read_word(tag, addr, 0);
    report(
        r == MemAccess::Ok,
        &format!("write test: read original at {}", hex(addr)),
        &format!("{} {}", r.name(), hex(original)),
    );
    if r != MemAccess::Ok {
        return;
    }
    for pattern in [0xA5C3_F00Fu32, 0x5A3C_0FF0] {
        let r = tag.write_memory(addr, &pattern.to_le_bytes(), false);
        let (rb, back) = if r == MemAccess::Ok {
            read_word(tag, addr, 0)
        } else {
            (r, 0)
        };
        report(
            r == MemAccess::Ok && rb == MemAccess::Ok && back == pattern,
            &format!("write test: {} reads back", hex(pattern)),
            &format!("{}/{} {}", r.name(), rb.name(), hex(back)),
        );
    }
    let r = tag.write_memory(addr, &original.to_le_bytes(), false);
    let (rb, restored) = if r == MemAccess::Ok {
        read_word(tag, addr, !original)
    } else {
        (r, !original)
    };
    report(
        r == MemAccess::Ok && rb == MemAccess::Ok && restored == original,
        "write test: original restored",
        &format!("{}/{} {}", r.name(), rb.name(), hex(restored)),
    );
}

/// Sends Start to a running tag and checks the error reports PERM. Refuses
/// to send Start unless the tag reports RUNNING or HIBERNATING, so it can
/// never start an idle tag.
fn start_running_test(tag: &Tag) {
    let what = "Start on running tag";
    let Some(status) = settled_status(tag) else {
        report(
            false,
            what,
            &format!("no definite state: {}", tag.last_error().message),
        );
        return;
    };
    if status.state != TagState::Running as i32 && status.state != TagState::Hibernating as i32 {
        report(
            false,
            what,
            &format!("tag is {}; Start not sent", state_name(status.state)),
        );
        return;
    }
    let cfg = match tag.config() {
        Ok(cfg) => cfg,
        Err(_) => {
            report(
                false,
                what,
                &format!("GetConfig failed: {}", tag.last_error().message),
            );
            return;
        }
    };
    let started = tag.start(&cfg).is_ok();
    let e = tag.last_error();
    report(
        !started && e.err == tagcore::proto::ack::Err::Perm && !e.transport_failed,
        "Start on running tag gives PERM without transport failure",
        &format!(
            "{}, err {}, transport_failed {}{}",
            if started {
                "Start succeeded"
            } else {
                "Start refused"
            },
            e.err.as_str_name(),
            e.transport_failed,
            if e.message.is_empty() {
                String::new()
            } else {
                format!(", \"{}\"", e.message)
            }
        ),
    );
}

/// Polls status every 100 ms for up to `seconds` while the operator unplugs
/// the base, and checks the failure is reported as a transport failure.
fn unplug_test(tag: &Tag, seconds: i32) {
    eprintln!("Unplug the base now (waiting up to {seconds} s)...");
    let what = "unplug reported as transport failure";
    let mut ok = 0;
    let end = Instant::now() + Duration::from_secs(seconds.max(0) as u64);
    while Instant::now() < end {
        if tag.status().is_err() {
            let e = tag.last_error();
            report(
                e.transport_failed,
                what,
                &format!(
                    "after {ok} good calls, err {}, transport_failed {}{}",
                    e.err.as_str_name(),
                    e.transport_failed,
                    if e.message.is_empty() {
                        String::new()
                    } else {
                        format!(", \"{}\"", e.message)
                    }
                ),
            );
            return;
        }
        ok += 1;
        sleep(Duration::from_millis(100));
    }
    report(false, what, "no failure within the wait");
}

/// Runs both threads for `seconds` and reports whether every call succeeded.
fn two_thread_check(tag: &Tag, seconds: i32) {
    let stop = AtomicBool::new(false);
    let (rpc_ok, rpc_fail, mem_ok, mem_fail) = (
        AtomicI32::new(0),
        AtomicI32::new(0),
        AtomicI32::new(0),
        AtomicI32::new(0),
    );
    std::thread::scope(|scope| {
        scope.spawn(|| {
            while !stop.load(Ordering::Relaxed) {
                let counter = if tag.status().is_ok() {
                    &rpc_ok
                } else {
                    &rpc_fail
                };
                counter.fetch_add(1, Ordering::Relaxed);
            }
        });
        scope.spawn(|| {
            let mut buf = [0u8; 256];
            while !stop.load(Ordering::Relaxed) {
                let counter = if tag.read_memory(SRAM_BASE, &mut buf) == MemAccess::Ok {
                    &mem_ok
                } else {
                    &mem_fail
                };
                counter.fetch_add(1, Ordering::Relaxed);
            }
        });
        sleep(Duration::from_secs(seconds as u64));
        stop.store(true, Ordering::Relaxed);
    });
    let (ro, rf, mo, mf) = (
        rpc_ok.into_inner(),
        rpc_fail.into_inner(),
        mem_ok.into_inner(),
        mem_fail.into_inner(),
    );
    report(
        rf == 0 && mf == 0 && ro > 0 && mo > 0,
        "GetStatus and ReadMemory from two threads",
        &format!("{ro}/{} GetStatus, {mo}/{} reads ok", ro + rf, mo + mf),
    );
}

/// Parses an address as `strtoul(text, &end, 0)` does, requiring the whole
/// string to be consumed and the value to fit 32 bits.
fn parse_address(text: &str) -> Option<u32> {
    let s = text.trim_start();
    let (negative, s) = match s.as_bytes().first() {
        Some(b'-') => (true, &s[1..]),
        Some(b'+') => (false, &s[1..]),
        _ => (false, s),
    };
    let (radix, digits) = if let Some(hex) = s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")) {
        (16, hex)
    } else if s.len() > 1 && s.starts_with('0') {
        (8, &s[1..])
    } else {
        (10, s)
    };
    if digits.is_empty() || !digits.chars().all(|c| c.is_digit(radix)) {
        return None;
    }
    let value = u64::from_str_radix(digits, radix).ok()?;
    // strtoul negates the magnitude modulo 2^64, which never fits 32 bits
    // unless it is zero.
    let value = if negative {
        value.wrapping_neg()
    } else {
        value
    };
    u32::try_from(value).ok()
}

fn finish() -> ExitCode {
    let n = failures();
    println!(
        "{} ({n} failure{})",
        if n != 0 { "FAILED" } else { "ALL PASSED" },
        if n == 1 { "" } else { "s" }
    );
    if n != 0 {
        ExitCode::FAILURE
    } else {
        ExitCode::SUCCESS
    }
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    init_logging(&cli.base);

    // The idle loop advances by the interval, so a zero one never ends.
    if cli.idle > 0 && cli.interval <= 0 {
        eprintln!("--interval must be at least 1 second with --idle");
        return ExitCode::FAILURE;
    }

    let tag = Tag::new();
    let dev = select_base::<Cli>(&tag, &cli.base);
    if !tag.attach(&dev) {
        eprintln!("Attach failed");
        return ExitCode::FAILURE;
    }

    // Which family: try the L4 DBGMCU ID code, then the U3 one only if that
    // fails, so neither family reads the other's address unnecessarily.
    let (r_l4, id_l4) = read_word(&tag, L4_DBGMCU_IDCODE, 0);
    let is_l4 = r_l4 == MemAccess::Ok && plausible_idcode(id_l4);
    let (r_u3, id_u3) = if is_l4 {
        (MemAccess::NotAttached, 0)
    } else {
        read_word(&tag, U3_DBGMCU_IDCODE, 0)
    };
    let is_u3 = !is_l4 && r_u3 == MemAccess::Ok && plausible_idcode(id_u3);
    let detail = if is_u3 {
        format!("STM32U3 {}", hex(id_u3))
    } else if is_l4 {
        format!("STM32L4 {}", hex(id_l4))
    } else {
        format!(
            "U3 {} {}, L4 {} {}",
            r_u3.name(),
            hex(id_u3),
            r_l4.name(),
            hex(id_l4)
        )
    };
    report(is_u3 || is_l4, "DBGMCU ID code", &detail);

    // Multi-chunk SRAM read.
    let mut sram = vec![0u8; 4096];
    let t0 = Instant::now();
    let r_sram = tag.read_memory(SRAM_BASE, &mut sram);
    let ms = t0.elapsed().as_secs_f64() * 1000.0;
    report(
        r_sram == MemAccess::Ok,
        "4 KB SRAM read",
        &format!("{}, {ms:.1} ms", r_sram.name()),
    );

    // Range checks: nothing reaches the link.
    let mut word = [0u8; 4];
    report(
        tag.read_memory(SRAM_BASE + 2, &mut word) == MemAccess::InvalidRange,
        "unaligned address refused",
        "",
    );
    report(
        tag.read_memory(SRAM_BASE, &mut word[..3]) == MemAccess::InvalidRange,
        "unaligned length refused",
        "",
    );
    report(
        tag.read_memory(0xFFFF_FFFC, &mut sram[..8]) == MemAccess::InvalidRange,
        "wrapping range refused",
        "",
    );

    // Guard: refused before anything is written.
    if is_u3 {
        let r = tag.write_memory(MONITOR_SHARED_ADDR + 0x10, &[0; 4], false);
        report(
            r == MemAccess::Protected,
            "U3 monitor_shared write refused",
            r.name(),
        );
    }

    if cli.threads > 0 {
        two_thread_check(&tag, cli.threads);
    }

    // last_error after a successful RPC.
    let got = tag.status();
    let e = tag.last_error();
    report(
        got.is_ok()
            && !e.transport_failed
            && e.err == tagcore::proto::ack::Err::Ok
            && e.message.is_empty(),
        "LastError clean after GetStatus",
        &match &got {
            Ok(status) => format!("state {}", state_name(status.state)),
            Err(_) => e.message.clone(),
        },
    );

    if let Some(text) = &cli.write_test {
        match parse_address(text) {
            Some(addr) => write_test(&tag, addr),
            None => report(false, "write test", &format!("cannot parse address {text}")),
        }
    }
    if cli.start_running {
        start_running_test(&tag);
    }
    if cli.unplug > 0 {
        unplug_test(&tag, cli.unplug);
        return finish();
    }

    // Keepalive, or the control without it.
    if cli.idle > 0 {
        let (mut calls, mut call_failures) = (0, 0);
        let mut t = 0;
        while t < cli.idle {
            sleep(Duration::from_secs(cli.interval.max(0) as u64));
            if !cli.no_keepalive {
                calls += 1;
                if !tag.keep_alive() {
                    call_failures += 1;
                }
            }
            t += cli.interval;
        }
        let alive = tag.status().is_ok();
        if cli.no_keepalive {
            report(
                !alive,
                "control: session ended after idle",
                &if alive {
                    "GetStatus still succeeds".to_owned()
                } else {
                    tag.last_error().message
                },
            );
        } else {
            report(
                alive && call_failures == 0,
                "session survives idle with KeepAlive",
                &format!("{calls} KeepAlive calls, {call_failures} failed"),
            );
        }
    }

    tag.detach();
    report(
        tag.read_memory(SRAM_BASE, &mut word) == MemAccess::NotAttached,
        "read after Detach refused",
        "",
    );
    finish()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn write_test_addresses_parse_strictly() {
        assert_eq!(parse_address("0x20001000"), Some(0x2000_1000));
        assert_eq!(parse_address("536875008"), Some(0x2000_1000));
        assert_eq!(parse_address("0x2000100g"), None);
        assert_eq!(parse_address(""), None);
        assert_eq!(parse_address("0x100000000"), None);
        assert_eq!(parse_address("0x+5"), None);
    }

    #[test]
    fn idcode_plausibility() {
        assert!(plausible_idcode(0x1001_6435));
        assert!(!plausible_idcode(0x1000_0000));
        assert!(!plausible_idcode(0xFFFF_FFFF));
    }
}
