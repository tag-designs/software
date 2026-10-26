//! `tag-info`: print a tag's information, configuration, status and state
//! log, or (`--json`) all of it as one JSON object for recording.
//!
//! Port of `host/commandline/tag-info.cc`. Attaching connects under reset,
//! so the state shown is the one the tag restored on boot, not the one it
//! was in.

use std::process::ExitCode;

use clap::Parser;
use prost_reflect::{Kind, ReflectMessage};
use tag_tools::{BaseArgs, enum_name, exit_on_interrupt, init_logging, select_base, state_name};
use tagcore::Tag;
use tagcore::proto::{self, State, TagInfo, state};
use tagcore::textformat::{format_g, scalar};

/// print tag information. Attaching connects under reset, so the state shown
/// is the one the tag restored on boot, not the one it was in: a tag past its
/// start time is reported RUNNING because the reset started it, and a tag
/// that had stalled reports whatever its backup registers held
#[derive(Parser)]
#[command(name = "tag-info")]
struct Cli {
    #[command(flatten)]
    base: BaseArgs,

    /// Print tag information as a single JSON object, for recording rather
    /// than reading
    #[arg(short, long)]
    json: bool,
}

/// IMUTag collection-init failure bits; see IMUTag inc/sensors.h.
const INIT_FAIL_LSM_CONFIG: u32 = 1 << 0;
const INIT_FAIL_MAG: u32 = 1 << 1;
const INIT_FAIL_PRESSURE: u32 = 1 << 2;
const INIT_FAIL_STATUS_SHIFT: u32 = 8;
const INIT_FAIL_STATUS_MASK: u32 = 0xff << INIT_FAIL_STATUS_SHIFT;

/// A float as `std::ostream` prints it by default (`%g`, six digits).
fn ostream_float(v: f32) -> String {
    format_g(f64::from(v), 6, 6, |_| true)
}

/// A marker epoch as UTC, or as a bare value when implausible: the tag
/// stores -1 for an erased slot, and an unset RTC gives an epoch near zero.
fn format_marker_time(millis: i64) -> String {
    let seconds = millis / 1000;
    if seconds <= 0 {
        return format!("{seconds}  <-- clock not set");
    }
    match chrono::DateTime::from_timestamp(seconds, 0) {
        Some(t) => t.format("%Y-%m-%d %H:%M:%S UTC").to_string(),
        None => seconds.to_string(),
    }
}

/// A marker's detail word, decoded for the reason that produced it. Only the
/// collection-init failure (EVENT_UNKNOWN) is decoded; anything else is shown
/// in hex rather than dropped, so newer firmware detail is still visible.
fn format_transition_detail(reason: i32, detail: u32) -> String {
    if reason != state::Event::Unknown as i32 {
        return format!("0x{detail:x}");
    }
    let mut stages = Vec::new();
    if detail & INIT_FAIL_LSM_CONFIG != 0 {
        stages.push("LSM6 config");
    }
    if detail & INIT_FAIL_MAG != 0 {
        stages.push("magnetometer");
    }
    if detail & INIT_FAIL_PRESSURE != 0 {
        stages.push("pressure");
    }
    let mut out = String::from("collection init failed: ");
    if stages.is_empty() {
        out.push_str(&format!("unrecognized (0x{detail:x})"));
    } else {
        out.push_str(&stages.join(", "));
    }
    let status = (detail & INIT_FAIL_STATUS_MASK) >> INIT_FAIL_STATUS_SHIFT;
    if status != 0 {
        out.push_str(&format!("; first driver status {status}"));
    }
    out
}

/// Prints the tag's persistent state-transition marker log.
///
/// The tag records one marker per transition into a fixed, non-wrapping
/// flash region that only a reset erases; once full, markers are dropped
/// silently, so the entry count is the diagnostic of interest.
fn print_state_log(tag: &Tag) {
    println!("State transition log:");
    let mut next = 0;
    // A tag that keeps returning entries must not hang the tool; capacity
    // is 10 + TagState_MAX on current firmware, so 16 pages is ample.
    for _ in 0..16 {
        let Ok(log) = tag.state_log(next) else { break };
        if log.states.is_empty() {
            break;
        }
        for marker in &log.states {
            print_marker(next, marker);
            next += 1;
        }
    }
    if next == 0 {
        println!(
            "  (empty -- nothing recorded since the last reset; reset recovery reads this as idle)"
        );
    } else {
        println!(
            "  entries: {next}  (compare against the tag's capacity, 10 + TagState_MAX; at \
             capacity, new markers are dropped silently and reset recovery may act on a stale \
             final entry)"
        );
    }
}

fn print_marker(index: i32, marker: &State) {
    let status = marker.status.clone().unwrap_or_default();
    println!(
        "  [{index}] {}  reason={}  time={}",
        state_name(status.state),
        enum_name(marker.transition_reason, state::Event::as_str_name),
        format_marker_time(status.millis)
    );
    // Only present on targets with room for it, and only when the
    // transition had something to say, so absence means nothing.
    if marker.transition_detail != 0 {
        println!(
            "        detail={}",
            format_transition_detail(marker.transition_reason, marker.transition_detail)
        );
    }
    println!(
        "        internal_pages={} external_pages={} vdd={} temp={}",
        status.internal_data_count,
        status.external_data_count,
        ostream_float(status.voltage),
        ostream_float(status.temperature)
    );
}

/// Prints info, config and status as one JSON object, serialised by the
/// library with the options tagcore uses to store them. Config and status
/// are included when available rather than required.
fn print_json(tag: &Tag) -> ExitCode {
    let Ok(info) = tag.tag_info() else {
        eprintln!("Info failed");
        return ExitCode::FAILURE;
    };
    let mut out = String::from("{");
    // Plain ASCII from the base firmware's target name and git metadata,
    // with nothing JSON would need escaped.
    if let Some(base) = tag.base_identity() {
        out.push_str(&format!("\"base\":\"{base}\","));
    }
    if let Some(sha) = tag.git_sha() {
        out.push_str(&format!("\"tag_sha\":\"{sha}\","));
    }
    out.push_str(&format!("\"info\":{}", proto::to_json(&info)));
    if let Ok(config) = tag.config() {
        out.push_str(&format!(",\"config\":{}", proto::to_json(&config)));
    }
    if let Ok(status) = tag.status() {
        out.push_str(&format!(",\"status\":{}", proto::to_json(&status)));
    }
    out.push('}');
    println!("{out}");
    ExitCode::SUCCESS
}

/// The C++ `FieldDescriptor::type_name()` of a field.
fn type_name(kind: &Kind) -> &'static str {
    match kind {
        Kind::Double => "double",
        Kind::Float => "float",
        Kind::Int64 => "int64",
        Kind::Uint64 => "uint64",
        Kind::Int32 => "int32",
        Kind::Fixed64 => "fixed64",
        Kind::Fixed32 => "fixed32",
        Kind::Bool => "bool",
        Kind::String => "string",
        Kind::Bytes => "bytes",
        Kind::Uint32 => "uint32",
        Kind::Sfixed32 => "sfixed32",
        Kind::Sfixed64 => "sfixed64",
        Kind::Sint32 => "sint32",
        Kind::Sint64 => "sint64",
        Kind::Enum(_) => "enum",
        Kind::Message(_) => "message",
    }
}

/// Human labels for some TagInfo fields.
fn label(name: &str) -> &str {
    match name {
        "monitor" => "Monitor Version",
        "board_desc" => "Board Description",
        "uuid" => "Chip UUID",
        "intflashsz" => "Internal Flash (kB)",
        "extflashsz" => "External Flash (kB)",
        "firmware" => "Firmware",
        "gitrepo" => "Git Repo",
        "githash" => "Git Hash",
        "build_time" => "Build Date",
        other => other,
    }
}

/// Lists every TagInfo field with its type, so a field added to the
/// protocol prints without a change here. A field with explicit presence
/// that is unset prints "(not set)", not a default indistinguishable from a
/// real zero; repeated fields are not expanded.
fn print_fields(info: &TagInfo) {
    let msg = info.transcode_to_dynamic();
    for field in msg.descriptor().fields() {
        let value = if field.is_list() || field.is_map() {
            "(repeated)".to_owned()
        } else if field.supports_presence() && !msg.has_field(&field) {
            "(not set)".to_owned()
        } else {
            match msg.get_field(&field).as_ref() {
                prost_reflect::Value::F32(v) => ostream_float(*v),
                prost_reflect::Value::F64(v) => format_g(*v, 6, 6, |_| true),
                prost_reflect::Value::String(s) => s.clone(),
                v => scalar(&field, v),
            }
        };
        println!(
            "{}: {} : {value}",
            type_name(&field.kind()),
            label(field.name())
        );
    }
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    init_logging(&cli.base);
    exit_on_interrupt();

    let tag = Tag::new();
    let dev = select_base::<Cli>(&tag, &cli.base);
    if !tag.attach(&dev) {
        return ExitCode::FAILURE;
    }
    if cli.json {
        return print_json(&tag);
    }

    match tag.base_identity() {
        Some(base) => println!("Base: {base}"),
        None => println!("Base: unidentified (its firmware predates the identify command)"),
    }
    if let Some(sha) = tag.git_sha() {
        println!("Tag SHA: {sha}");
    }
    let info = match tag.tag_info() {
        Ok(info) => info,
        Err(_) => {
            eprintln!("Info failed");
            return ExitCode::FAILURE;
        }
    };
    println!("{}", proto::debug_string(&info));
    print_fields(&info);

    // A default-constructed config would misreport the tag's settings.
    match tag.config() {
        Ok(config) => println!("{}", proto::debug_string(&config)),
        Err(_) => eprintln!("GetConfig failed: {}", tag.debug_message()),
    }

    if let Ok(status) = tag.status() {
        println!("Current state: {}", state_name(status.state));
        // A boot reset-recovery trace when the firmware has nothing else to
        // report; see statusRecoveryTraceWrite().
        if !status.debug_message.is_empty() {
            println!("Firmware report: {}", status.debug_message);
        }
    }
    print_state_log(&tag);
    ExitCode::SUCCESS
}
