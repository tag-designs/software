//! Read-only tag report: probe, voltage, firmware, info, status, config and
//! state log. Sends no request that changes the tag.
//!
//! Attaching resets the tag, so do not run this against a tag in the middle
//! of a deployment run.
//!
//! ```sh
//! RUST_LOG=info cargo run --example tag_info [-- <ST-LINK serial>]
//! ```

use std::process::ExitCode;

use tagcore::{Tag, UsbDev};

/// Lists probes, attaches to the one named by serial (or the first), prints
/// the report and detaches; exits non-zero if attach, info, status or the
/// state log fails.
fn main() -> ExitCode {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("warn")).init();

    let tag = Tag::new();
    let probes = tag.available();
    for p in &probes {
        println!(
            "probe {:04x}:{:04x} bus {:03} addr {:03} serial {}",
            p.vid, p.pid, p.bus, p.address, p.serial
        );
    }

    let wanted = std::env::args().nth(1);
    let usbdev = match &wanted {
        Some(sn) => match probes.iter().find(|p| &p.serial == sn) {
            Some(p) => p.clone(),
            None => {
                eprintln!("no ST-LINK with serial {sn}");
                return ExitCode::FAILURE;
            }
        },
        None => UsbDev::default(),
    };

    if !tag.attach(&usbdev) {
        eprintln!("attach failed");
        return ExitCode::FAILURE;
    }

    match tag.voltage() {
        Some(v) => println!("voltage (ST-LINK) {v:.3} V"),
        None => println!("voltage (ST-LINK) unavailable"),
    }
    println!("firmware sha {}", tag.git_sha().unwrap_or_default());

    let mut ok = true;
    match tag.tag_info() {
        Ok(info) => println!("info {info:#?}"),
        Err(e) => {
            ok = false;
            eprintln!("info failed: {e}");
        }
    }
    match tag.status() {
        Ok(status) => println!("status {status:#?}"),
        Err(e) => {
            ok = false;
            eprintln!("status failed: {e}");
        }
    }
    match tag.config() {
        Ok(config) => println!("config {config:#?}"),
        Err(e) => eprintln!("config: {e}"),
    }

    let mut index = 0;
    loop {
        match tag.state_log(index) {
            Ok(log) if log.states.is_empty() => break,
            Ok(log) => {
                for state in &log.states {
                    println!("state[{index}] {state:?}");
                    index += 1;
                }
            }
            Err(e) => {
                eprintln!("state log failed at {index}: {e}");
                ok = false;
                break;
            }
        }
    }
    println!("state log entries {index}");

    let link = tag.link_stats();
    let rpc = tag.monitor_stats();
    println!(
        "rpcs {} ({} B out, {} B in, {:.1} ms total); usb {} out / {} in",
        rpc.rpc_calls,
        rpc.request_bytes,
        rpc.response_bytes,
        rpc.rpc_total_ns as f64 / 1e6,
        link.usb_out_calls,
        link.usb_in_calls
    );

    tag.detach();
    if ok {
        ExitCode::SUCCESS
    } else {
        ExitCode::FAILURE
    }
}
