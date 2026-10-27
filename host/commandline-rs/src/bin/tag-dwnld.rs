//! `tag-dwnld`: download a stopped tag's log into a SQLite database.
//!
//! Port of `host/commandline/dwnld.cc`. The tag side uses tagcore-rs; the
//! database is written by the C++ writer (`SqliteTagLogWriter`) through the
//! `taglogwriter` crate and the `tagcore_c` shared library, so the file is
//! the one the C++ `tag-dwnld` writes from the same tag.
//!
//! The sequence, as in the C++:
//!
//! 1. Check `--format` before attaching, since attaching resets the tag.
//! 2. Attach, then poll status until the tag reports a definite state.
//! 3. With `--rescue-exception`, turn an old BitTag's EXCEPTION into
//!    ABORTED; with `--stop`, stop a RUNNING tag and poll until it stops.
//! 4. Refuse unless the tag is FINISHED, ABORTED or EXCEPTION.
//! 5. Read the config, open the database, and write the header from the
//!    same requests, in the same order, as `readTagLogHeader()`.
//! 6. Request pages from index 0. The writer decodes each one and says how
//!    many records it consumed; the next request is at the index advanced
//!    by that, and a page that consumes nothing ends the log. A NODATA page
//!    below the tag's record count is skipped over.
//!
//! Exits 0 when the log is written (or there was nothing to download), 1 on
//! any failure, 2 on an option error (clap).
//!
//! Built only with the `sqlitelog` feature, which needs `libtagcore_c`; see
//! `host/libraries/taglogwriter-rs/README.md`.
//!
//! # Hardware side effects
//!
//! The attach resets the tag; `--stop` ends a run; `--rescue-exception`
//! rewrites one backup-state word. Downloading itself changes nothing on the
//! tag, so a download can be repeated.

use std::io::Write as _;
use std::process::ExitCode;
use std::thread::sleep;
use std::time::{Duration, Instant};

use clap::Parser;
use tag_tools::{BaseArgs, exit_on_interrupt, init_logging, select_base, state_name};
use tagcore::proto::{self, Ack, Status, TagState, TagType, ack};
use tagcore::{LinkAdaptStats, Tag, TagMonitorStats};
use taglogwriter::{LogHeader, LogWriter};

/// Downloads data from a tag
#[derive(Parser)]
#[command(name = "tag-dwnld")]
struct Cli {
    /// The shared `--debug` and `--base` options.
    #[command(flatten)]
    base: BaseArgs,

    /// Output file path. Defaults to tag-download.db3
    #[arg(short, long, value_name = "PATH")]
    output: Option<String>,

    /// Output format: default or sqlite (the text format is retired)
    #[arg(short, long, default_value = "default")]
    format: String,

    /// Stop a running tag before downloading
    #[arg(short, long)]
    stop: bool,

    /// For old firmware only: change EXCEPTION backup state to ABORTED before
    /// downloading
    #[arg(long)]
    rescue_exception: bool,

    /// Print download timing and link transport profile
    #[arg(long)]
    profile: bool,

    /// Seconds to wait after attach for the tag to report a definite state.
    /// Attach connects under reset, so the first status can legitimately be
    /// STATE_UNSPECIFIED while the tag boots
    #[arg(long, default_value_t = 10, value_name = "SECONDS")]
    settle_timeout: u64,

    /// With --stop, seconds to wait for the tag to reach FINISHED or ABORTED.
    /// Stop() returns when the request is accepted; the state machine acts on
    /// it later
    #[arg(long, default_value_t = 30, value_name = "SECONDS")]
    stop_timeout: u64,
}

/// Prints `message` to stderr and returns exit status 1.
fn fail(message: impl std::fmt::Display) -> ExitCode {
    eprintln!("{message}");
    ExitCode::FAILURE
}

/// Checks `--format` without touching the tag: `Ok` for SQLite (the only
/// format), `Err` with the message otherwise.
///
/// # Errors
///
/// For the retired text format and for an unknown name.
fn check_format(name: &str) -> Result<(), String> {
    match name.to_lowercase().as_str() {
        "default" | "sqlite" | "sql" | "db" | "db3" => Ok(()),
        "text" | "txt" => Err("The text log format is retired; downloads are written as \
                               SQLite. tag-txt-import converts existing .txt logs."
            .into()),
        _ => Err(format!("Unknown output format: {name}")),
    }
}

/// Polls status a second apart, at least once, until the tag reports a
/// state other than STATE_UNSPECIFIED, and returns that status.
///
/// # Errors
///
/// When a status read fails (with the tag's own reason), or the tag is
/// still unsettled after `timeout_s`.
fn settle(tag: &Tag, timeout_s: u64) -> Result<Status, String> {
    for _ in 0..timeout_s.max(1) {
        let status = tag
            .status()
            .map_err(|_| format!("Could not read tag status: {}", tag.debug_message()))?;
        if status.state != TagState::StateUnspecified as i32 {
            return Ok(status);
        }
        sleep(Duration::from_secs(1));
    }
    Err(format!(
        "tag still reports STATE_UNSPECIFIED after {timeout_s} s; it is not merely settling"
    ))
}

/// Stops a running tag and polls every 2 s, at least once, until it reaches
/// FINISHED or ABORTED; returns the final status.
///
/// # Errors
///
/// When the stop or a status read fails, or the tag has not stopped after
/// `timeout_s`.
fn stop(tag: &Tag, timeout_s: u64) -> Result<Status, String> {
    tag.stop()
        .map_err(|_| format!("Could not stop running tag: {}", tag.debug_message()))?;
    let mut status = Status::default();
    for _ in 0..timeout_s.div_ceil(2).max(1) {
        status = tag.status().map_err(|_| {
            format!(
                "Could not read tag status after stop: {}",
                tag.debug_message()
            )
        })?;
        if status.state == TagState::Finished as i32 || status.state == TagState::Aborted as i32 {
            return Ok(status);
        }
        sleep(Duration::from_secs(2));
    }
    Err(format!(
        "Tag did not reach a stopped state within {timeout_s} s; last state {}",
        state_name(status.state)
    ))
}

/// Whether a download may run in `state`. EXCEPTION counts: collection is
/// no longer changing the log, and field recovery may depend on reading it.
fn is_downloadable(state: i32) -> bool {
    state == TagState::Finished as i32
        || state == TagState::Aborted as i32
        || state == TagState::Exception as i32
}

/// Reads the log header with the requests, in the order, that the C++
/// `readTagLogHeader()` makes: Config, TagInfo, calibration constants from
/// index 0 until one is refused, then the state log in chunks until one is
/// refused or empty.
///
/// # Errors
///
/// When the config or the info cannot be read.
fn read_header(tag: &Tag) -> Result<LogHeader, String> {
    let config = tag
        .config()
        .map_err(|_| "Could not read tag config".to_string())?;
    let info = tag
        .tag_info()
        .map_err(|_| "Could not read tag info".to_string())?;
    let mut calibration = Vec::new();
    while let Ok(constants) = tag.read_calibration(calibration.len() as i32) {
        calibration.push(constants);
    }
    let mut states = Vec::new();
    while let Ok(chunk) = tag.state_log(states.len() as i32) {
        if chunk.states.is_empty() {
            break;
        }
        states.extend(chunk.states);
    }
    Ok(LogHeader {
        config,
        info,
        calibration,
        states,
    })
}

/// Draws the 40-column progress bar on stdout, as the C++ does.
fn print_progress(current: usize, total: usize) {
    const WIDTH: usize = 40;
    let ratio = if total > 0 {
        (current as f64 / total as f64).min(1.0)
    } else {
        1.0
    };
    let filled = (ratio * WIDTH as f64) as usize;
    let bar: String = (0..WIDTH)
        .map(|i| if i < filled { '#' } else { '-' })
        .collect();
    print!(
        "\r[{bar}] {:>3}% {current}/{total}",
        (ratio * 100.0) as usize
    );
    let _ = std::io::stdout().flush();
}

/// Prints one timing line, with the per-request figure when `count` > 0.
fn duration_line(label: &str, total: Duration, count: u64) {
    let seconds = total.as_secs_f64();
    if count > 0 {
        println!(
            "{label}: {seconds} s total, {} s/request",
            seconds / count as f64
        );
    } else {
        println!("{label}: {seconds} s");
    }
}

/// Prints one transfer line: calls, bytes, time and rate.
fn transfer_line(label: &str, calls: u64, bytes: u64, ns: u64) {
    let seconds = ns as f64 / 1e9;
    let mut line = format!("{label}: calls={calls} bytes={bytes} time={seconds} s");
    if ns > 0 {
        line += &format!(" rate={} KiB/s", bytes as f64 / 1024.0 / seconds);
    }
    println!("{line}");
}

/// Prints `--profile`: the download's own timing, then the monitor RPC and
/// link transport counters.
fn print_profile(
    link: &LinkAdaptStats,
    monitor: &TagMonitorStats,
    get_log: Duration,
    write_log: Duration,
    requests: u64,
) {
    let ns = |ns: u64| Duration::from_nanos(ns);
    println!("\nDownload profile");
    duration_line("GetDataLog wait", get_log, requests);
    duration_line("Decode/write log", write_log, requests);
    if !link.enabled || !monitor.enabled {
        println!("Host link instrumentation was compiled out");
        return;
    }
    println!("\nMonitor RPC");
    println!(
        "rpc calls={} request_bytes={} response_bytes={}",
        monitor.rpc_calls, monitor.request_bytes, monitor.response_bytes
    );
    let calls = monitor.rpc_calls;
    duration_line("rpc total", ns(monitor.rpc_total_ns), calls);
    duration_line("serialize request", ns(monitor.serialize_ns), calls);
    duration_line("write request buffer", ns(monitor.write_request_ns), calls);
    duration_line("monitor call", ns(monitor.monitor_call_ns), calls);
    duration_line("read response buffer", ns(monitor.read_response_ns), calls);
    duration_line("parse response", ns(monitor.parse_ns), calls);
    println!("\nLink transport");
    transfer_line(
        "usb out",
        link.usb_out_calls,
        link.usb_out_bytes,
        link.usb_out_ns,
    );
    transfer_line(
        "usb in",
        link.usb_in_calls,
        link.usb_in_bytes,
        link.usb_in_ns,
    );
    duration_line("stlink command", ns(link.cmd_ns), link.cmd_calls);
    transfer_line(
        "read mem32",
        link.read_mem_calls,
        link.read_mem_bytes,
        link.read_mem_ns,
    );
    transfer_line(
        "write mem32",
        link.write_mem_calls,
        link.write_mem_bytes,
        link.write_mem_ns,
    );
    duration_line(
        "read debug32",
        ns(link.read_debug_ns),
        link.read_debug_calls,
    );
    duration_line(
        "write debug32",
        ns(link.write_debug_ns),
        link.write_debug_calls,
    );
    duration_line("rw status", ns(link.rw_status_ns), link.rw_status_calls);
}

/// Attaches, brings the tag to a downloadable state, and writes the log.
fn main() -> ExitCode {
    let cli = Cli::parse();
    init_logging(&cli.base);
    taglogwriter::set_debug(cli.base.debug);

    // Before attaching, which resets the tag: a mistyped option must not cost
    // a running tag its run.
    if let Err(message) = check_format(&cli.format) {
        return fail(message);
    }

    let tag = Tag::new();
    let dev = select_base::<Cli>(&tag, &cli.base);
    if !tag.attach(&dev) {
        return fail("Attach failed");
    }
    exit_on_interrupt();

    let mut status = match settle(&tag, cli.settle_timeout) {
        Ok(status) => status,
        Err(message) => return fail(message),
    };
    if cli.base.debug && !status.debug_message.is_empty() {
        eprint!("{}", status.debug_message);
    }

    let rescue = cli.rescue_exception;
    if status.state == TagState::Exception as i32 && rescue {
        // Old BitTag firmware latched EXCEPTION but refused log reads unless
        // ABORTED or FINISHED; rewriting only that word is the least invasive
        // unlock. The helper verifies the write itself.
        eprintln!("Rescue: changing monitor-visible state from EXCEPTION to ABORTED");
        if !tag.force_backup_state(TagState::Aborted) {
            return fail(format!(
                "Could not rescue exception state: {}",
                tag.debug_message()
            ));
        }
        status.state = TagState::Aborted as i32;
        eprintln!("Rescue: backup state verified; continuing with pre-rescue log counts");
    }

    if status.state == TagState::Running as i32 && cli.stop {
        status = match stop(&tag, cli.stop_timeout) {
            Ok(status) => status,
            Err(message) => return fail(message),
        };
    }

    if !is_downloadable(status.state) {
        eprintln!("Can't dump logs from current state");
        eprintln!("{}", proto::debug_string(&status));
        return ExitCode::FAILURE;
    }

    let config = match tag.config() {
        Ok(config) => config,
        Err(_) => {
            return fail(format!(
                "Could not read tag config: {}",
                tag.debug_message()
            ));
        }
    };
    let output = cli.output.unwrap_or_else(|| "tag-download.db3".into());
    // The writer refuses a tag type it has no schema for before creating
    // the file.
    let mut writer = match LogWriter::create(output.as_ref(), &config, true) {
        Ok(writer) => writer,
        Err(error) => return fail(format!("Could not open output: {error}")),
    };
    println!("Writing sqlite log to {output}");

    let header = match read_header(&tag) {
        Ok(header) => header,
        Err(message) => return fail(format!("Could not write log header: {message}")),
    };
    if let Err(error) = writer.write_header(&header) {
        return fail(format!("Could not write log header: {error}"));
    }

    let bittag_rescue = rescue && config.tag_type == TagType::Bittag as i32;
    let mut max_count = if status.external_data_count > 0 {
        status.external_data_count as usize
    } else {
        status.internal_data_count.max(0) as usize
    };
    if bittag_rescue && max_count == 0 {
        // Some exception paths leave the page count at zero although the
        // BitTag's internal flash headers are intact; count them directly.
        match tag.count_bittag_log_headers() {
            Some(count) => max_count = count,
            None => return fail("Could not scan BitTag log headers"),
        }
        eprintln!("Rescue: found {max_count} BitTag log records");
    }
    if max_count == 0 {
        println!("No log records to download");
        return ExitCode::SUCCESS;
    }

    let (mut total, mut requests) = (0usize, 0u64);
    let (mut get_log, mut write_log) = (Duration::ZERO, Duration::ZERO);
    print_progress(total, max_count);
    tag.reset_link_stats();
    tag.reset_monitor_stats();

    let start = Instant::now();
    if let Err(error) = writer.begin_log() {
        println!();
        return fail(format!("Could not begin log download: {error}"));
    }
    write_log += start.elapsed();

    loop {
        let start = Instant::now();
        // In BitTag rescue mode the firmware's data-log RPC is avoided: the
        // host reads the raw headers from flash, wrapped as ordinary pages.
        let page: Option<Ack> = if bittag_rescue {
            tag.bittag_log_from_flash(total as i32)
        } else {
            tag.data_log(total as i32).ok()
        };
        let Some(page) = page else {
            println!();
            return fail(format!(
                "GetDataLog failed at entry {total}: {}",
                tag.debug_message()
            ));
        };
        get_log += start.elapsed();
        requests += 1;

        if !page.error_message().is_empty() {
            println!();
            return fail(page.error_message());
        }
        if page.err == ack::Err::Nodata as i32 && total < max_count {
            total += 1;
            print_progress(total, max_count);
            continue;
        }

        let start = Instant::now();
        let consumed = match writer.write_log(&page) {
            Ok(consumed) => consumed,
            Err(error) => {
                println!();
                return fail(format!("Could not write log data: {error}"));
            }
        };
        write_log += start.elapsed();
        if consumed == 0 {
            break;
        }
        total += consumed;
        print_progress(total, max_count);
    }

    let start = Instant::now();
    if let Err(error) = writer.end_log() {
        println!();
        return fail(format!("Could not finish log download: {error}"));
    }
    write_log += start.elapsed();

    println!();
    println!("Downloaded {total} records in {requests} requests");
    println!("Decoding time {} seconds", write_log.as_secs_f64());
    if cli.profile {
        print_profile(
            &tag.link_stats(),
            &tag.monitor_stats(),
            get_log,
            write_log,
            requests,
        );
    }
    ExitCode::SUCCESS
}
