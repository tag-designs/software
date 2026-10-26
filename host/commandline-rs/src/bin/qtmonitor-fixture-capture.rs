//! `qtmonitor-fixture-capture`: capture qtmonitor fake-tag fixture data from
//! an attached tag.
//!
//! Port of `host/commandline/qtmonitor-fixture-capture.cc`. Reads the monitor
//! information, configuration, status, and voltage responses used by
//! qtmonitor and serializes them as protobuf JSON inside the documentation
//! fixture schema (`host/docs/fixtures/qtmonitor/`). The tool is
//! intentionally read-only: it does not start, stop, erase, test, or
//! otherwise drive tag state.

use std::fmt::Write as _;
use std::path::Path;
use std::process::ExitCode;

use clap::Parser;
use log::LevelFilter;
use prost_reflect::{DynamicMessage, Kind, ReflectMessage, Value};
use tag_tools::{exit_on_interrupt, init_logging_at, parse_bus_device, state_name};
use tagcore::proto::{Config, Status, TagInfo, TagState, TagType};
use tagcore::textformat::format_g;
use tagcore::{Tag, UsbDev};

/// capture qtmonitor fake-tag fixture data from an attached tag
#[derive(Parser)]
#[command(name = "qtmonitor-fixture-capture")]
struct Cli {
    /// Select bus:device
    #[arg(short, long, value_name = "BUS:DEVICE")]
    base: Option<String>,

    /// Set log level to DEBUG
    #[arg(short, long)]
    debug: bool,

    /// Output fixture JSON path, or '-' for stdout
    #[arg(short, long, default_value = "qtmonitor-fixture.json")]
    output: String,

    /// Fixture id; defaults to the captured tag type
    #[arg(long)]
    id: Option<String>,

    /// Human-readable fixture label; defaults to the tag type
    #[arg(long)]
    label: Option<String>,

    /// Optional fixture notes
    #[arg(long)]
    notes: Option<String>,

    /// Source-tree default config path kept as fallback_ref
    #[arg(long)]
    fallback_config: Option<String>,

    /// Name for the captured status slot
    #[arg(long, default_value = "current")]
    status_name: String,

    /// Alias for --status-name; does not change tag state
    #[arg(long)]
    state: Option<String>,

    /// Capture current GetConfig() as the fixture's default config; this is
    /// the default
    #[arg(long)]
    capture_default_config: bool,

    /// Do not include the captured Config message
    #[arg(long)]
    no_config: bool,

    /// Replace hardware UUID and local source path placeholders
    #[arg(long)]
    sanitize: bool,

    /// Print captured tag summary to stderr
    #[arg(long)]
    print_summary: bool,
}

/// The options after parsing, as the C++ `CaptureOptions` holds them.
struct Capture {
    base: String,
    output: String,
    id: String,
    label: String,
    notes: String,
    fallback_config: String,
    status_name: String,
    capture_default_config: bool,
    sanitize: bool,
    print_summary: bool,
    debug: bool,
}

impl From<Cli> for Capture {
    fn from(cli: Cli) -> Self {
        Self {
            base: cli.base.unwrap_or_default(),
            output: cli.output,
            id: cli.id.unwrap_or_default(),
            label: cli.label.unwrap_or_default(),
            notes: cli.notes.unwrap_or_default(),
            fallback_config: cli.fallback_config.unwrap_or_default(),
            // --state wins over --status-name, as the C++ reads it second.
            status_name: cli.state.unwrap_or(cli.status_name),
            capture_default_config: !cli.no_config,
            sanitize: cli.sanitize,
            print_summary: cli.print_summary,
            debug: cli.debug,
        }
    }
}

/// Protobuf enum names as stable lower-kebab fixture ids.
fn lower_kebab(value: &str) -> String {
    value
        .chars()
        .map(|c| match c {
            '_' | ' ' => '-',
            c => c.to_ascii_lowercase(),
        })
        .collect()
}

/// Escapes a string for the hand-written JSON envelope.
fn json_escape(value: &str) -> String {
    let mut out = String::new();
    for c in value.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\u{8}' => out.push_str("\\b"),
            '\u{c}' => out.push_str("\\f"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => write!(out, "\\u{:04x}", c as u32).unwrap(),
            c => out.push(c),
        }
    }
    out
}

/// The current UTC time as ISO-8601, e.g. `2026-10-26T12:01:09Z`.
fn utc_timestamp() -> String {
    chrono::Utc::now().format("%Y-%m-%dT%H:%M:%SZ").to_string()
}

fn tag_type_name(tag_type: i32) -> String {
    TagType::try_from(tag_type)
        .map(|t| t.as_str_name().to_owned())
        .unwrap_or_else(|_| tag_type.to_string())
}

/// Serialises a message in the fixture JSON convention: the C++
/// `MessageToJsonString` with `add_whitespace`, `preserve_proto_field_names`
/// and always-print-fields-without-presence.
///
/// One space of indent per level, fields in field-number order, every field
/// without presence printed even at its default, unset messages and unset
/// `optional`/oneof fields omitted, 64-bit integers as strings, enums by
/// name, floats as the C++ library writes them. Ends with a newline, as the
/// C++ output does.
fn message_to_json<M: ReflectMessage>(msg: &M) -> String {
    let mut out = String::new();
    write_message(&msg.transcode_to_dynamic(), 0, &mut out);
    out.push('\n');
    out
}

fn indent(level: usize) -> String {
    " ".repeat(level)
}

fn write_message(msg: &DynamicMessage, level: usize, out: &mut String) {
    let mut fields: Vec<_> = msg
        .descriptor()
        .fields()
        .filter(|f| f.is_list() || f.is_map() || !f.supports_presence() || msg.has_field(f))
        .collect();
    if fields.is_empty() {
        out.push_str("{}");
        return;
    }
    fields.sort_by_key(|f| f.number());
    out.push_str("{\n");
    for (i, field) in fields.iter().enumerate() {
        out.push_str(&indent(level + 1));
        write!(out, "\"{}\": ", json_escape(field.name())).unwrap();
        write_value(&field.kind(), &msg.get_field(field), level + 1, out);
        if i + 1 < fields.len() {
            out.push(',');
        }
        out.push('\n');
    }
    out.push_str(&indent(level));
    out.push('}');
}

fn write_value(kind: &Kind, value: &Value, level: usize, out: &mut String) {
    match value {
        Value::Bool(b) => write!(out, "{b}").unwrap(),
        Value::I32(v) => write!(out, "{v}").unwrap(),
        Value::U32(v) => write!(out, "{v}").unwrap(),
        Value::I64(v) => write!(out, "\"{v}\"").unwrap(),
        Value::U64(v) => write!(out, "\"{v}\"").unwrap(),
        Value::F32(v) => {
            let v = *v;
            out.push_str(&json_float(f64::from(v), 6, 9, |s| {
                s.parse::<f32>().ok() == Some(v)
            }))
        }
        Value::F64(v) => {
            let v = *v;
            out.push_str(&json_float(v, 15, 17, |s| s.parse::<f64>().ok() == Some(v)))
        }
        Value::String(s) => write!(out, "\"{}\"", json_escape(s)).unwrap(),
        Value::Bytes(b) => write!(out, "\"{}\"", base64(b)).unwrap(),
        Value::EnumNumber(n) => match kind.as_enum().and_then(|e| e.get_value(*n)) {
            Some(v) => write!(out, "\"{}\"", v.name()).unwrap(),
            None => write!(out, "{n}").unwrap(),
        },
        Value::Message(m) => write_message(m, level, out),
        Value::List(items) => {
            if items.is_empty() {
                out.push_str("[]");
                return;
            }
            out.push_str("[\n");
            for (i, item) in items.iter().enumerate() {
                out.push_str(&indent(level + 1));
                write_value(kind, item, level + 1, out);
                if i + 1 < items.len() {
                    out.push(',');
                }
                out.push('\n');
            }
            out.push_str(&indent(level));
            out.push(']');
        }
        Value::Map(_) => out.push_str("{}"), // tag.proto has no map fields
    }
}

/// A float as proto3 JSON written by the C++ library: `SimpleFtoa` /
/// `SimpleDtoa` digits (`short`, else `long` significant digits when that
/// does not round-trip), non-finite values as the strings proto3 requires.
fn json_float(v: f64, short: usize, long: usize, round_trips: impl Fn(&str) -> bool) -> String {
    if v.is_nan() {
        return "\"NaN\"".into();
    }
    if v.is_infinite() {
        return if v > 0.0 {
            "\"Infinity\""
        } else {
            "\"-Infinity\""
        }
        .into();
    }
    format_g(v, short, long, round_trips)
}

/// Standard base64 with padding, as proto3 JSON writes `bytes`.
fn base64(data: &[u8]) -> String {
    const TABLE: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::new();
    for chunk in data.chunks(3) {
        let b = [
            chunk[0],
            *chunk.get(1).unwrap_or(&0),
            *chunk.get(2).unwrap_or(&0),
        ];
        let n = (u32::from(b[0]) << 16) | (u32::from(b[1]) << 8) | u32::from(b[2]);
        for i in 0..4 {
            if i <= chunk.len() {
                out.push(TABLE[((n >> (18 - 6 * i)) & 63) as usize] as char);
            } else {
                out.push('=');
            }
        }
    }
    out
}

/// Chooses the base, requiring --base when more than one is found.
fn select_device(tag: &Tag, capture: &Capture) -> Option<UsbDev> {
    let devs = tag.available();
    if devs.is_empty() {
        eprintln!("No tag bases found");
        return None;
    }

    if !capture.base.is_empty() {
        let Some((bus, address)) = parse_bus_device(&capture.base) else {
            eprintln!("Invalid --base value; expected bus:device");
            return None;
        };
        if let Some(dev) = devs.iter().find(|d| d.bus == bus && d.address == address) {
            return Some(dev.clone());
        }
        eprintln!("No matching device {}", capture.base);
        return None;
    }

    if devs.len() > 1 {
        eprintln!("Multiple tag bases found; select one with --base:");
        for d in &devs {
            eprintln!("  {}:{} 0x{:x}:0x{:x}", d.bus, d.address, d.vid, d.pid);
        }
        return None;
    }
    Some(devs[0].clone())
}

/// Replaces hardware- and workstation-specific TagInfo fields.
fn sanitize_info(info: &mut TagInfo, id: &str) {
    info.uuid = "000000000000000000000000".into();
    info.source_path = if id.is_empty() {
        format!(
            "/embedded/tags/{}",
            lower_kebab(&tag_type_name(info.tag_type))
        )
    } else {
        format!("/embedded/tags/{id}")
    };
}

/// Clears `sectors_erased` unless the tag is mid-reset, where it means
/// something.
fn sanitize_status(status: &mut Status) {
    if status.state != TagState::SReset as i32 {
        status.sectors_erased = 0;
    }
}

/// Composes the fixture and writes it to the output path, or stdout for `-`.
fn write_fixture(
    capture: &Capture,
    info: &TagInfo,
    config: Option<&Config>,
    status: &Status,
    voltage: f32,
) -> bool {
    let info_json = message_to_json(info);
    let config_json = config.map(message_to_json);
    let status_json = message_to_json(status);

    let type_name = tag_type_name(info.tag_type);
    let id = if capture.id.is_empty() {
        lower_kebab(&type_name)
    } else {
        capture.id.clone()
    };
    let label = if capture.label.is_empty() {
        type_name
    } else {
        capture.label.clone()
    };

    let mut out = String::from("{\n");
    out += "  \"schema\": \"tag-designs.qtmonitor.fake-tag.v1\",\n";
    writeln!(out, "  \"id\": \"{}\",", json_escape(&id)).unwrap();
    writeln!(out, "  \"label\": \"{}\",", json_escape(&label)).unwrap();
    if !capture.notes.is_empty() {
        writeln!(out, "  \"notes\": \"{}\",", json_escape(&capture.notes)).unwrap();
    }
    writeln!(out, "  \"captured_at_utc\": \"{}\",", utc_timestamp()).unwrap();
    writeln!(out, "  \"info\": {info_json},").unwrap();
    if let Some(config_json) = config_json {
        out += "  \"config\": {\n";
        out += "    \"source\": \"captured-default\"";
        if !capture.fallback_config.is_empty() {
            write!(
                out,
                ",\n    \"fallback_ref\": \"{}\"",
                json_escape(&capture.fallback_config)
            )
            .unwrap();
        }
        writeln!(out, ",\n    \"value\": {config_json}").unwrap();
        out += "  },\n";
    }
    writeln!(out, "  \"voltage\": {:.3},", f64::from(voltage)).unwrap();
    out += "  \"statuses\": {\n";
    writeln!(
        out,
        "    \"{}\": {status_json}",
        json_escape(&capture.status_name)
    )
    .unwrap();
    out += "  }\n";
    out += "}\n";

    if capture.output == "-" {
        print!("{out}");
        return true;
    }

    let path = Path::new(&capture.output);
    if let Some(parent) = path.parent().filter(|p| !p.as_os_str().is_empty())
        && let Err(e) = std::fs::create_dir_all(parent)
    {
        eprintln!("Could not create output directory {parent:?}: {e}");
        return false;
    }
    let mut file = match std::fs::File::create(path) {
        Ok(file) => file,
        Err(_) => {
            eprintln!("Could not open {} for writing", capture.output);
            return false;
        }
    };
    if std::io::Write::write_all(&mut file, out.as_bytes()).is_err() {
        eprintln!("Could not write {}", capture.output);
        return false;
    }
    true
}

fn print_summary(capture: &Capture, info: &TagInfo, status: &Status, voltage: f32) {
    eprintln!("Captured qtmonitor fixture");
    eprintln!("  tag_type: {}", tag_type_name(info.tag_type));
    eprintln!("  firmware: {}", info.firmware);
    eprintln!("  githash: {}", info.githash);
    eprintln!(
        "  state[{}]: {}",
        capture.status_name,
        state_name(status.state)
    );
    eprintln!(
        "  counts: internal={} external={}",
        status.internal_data_count, status.external_data_count
    );
    eprintln!("  voltage: {:.3}", f64::from(voltage));
    eprintln!("  output: {}", capture.output);
}

fn main() -> ExitCode {
    let capture = Capture::from(Cli::parse());
    // Quiet unless --debug.
    init_logging_at(if capture.debug {
        LevelFilter::Debug
    } else {
        LevelFilter::Off
    });

    let tag = Tag::new();
    let Some(dev) = select_device(&tag, &capture) else {
        return ExitCode::FAILURE;
    };
    if !tag.attach(&dev) {
        eprintln!("Attach failed");
        return ExitCode::FAILURE;
    }
    exit_on_interrupt();

    let mut info = match tag.tag_info() {
        Ok(info) => info,
        Err(_) => {
            eprintln!("Could not read tag info: {}", tag.debug_message());
            return ExitCode::FAILURE;
        }
    };
    let config = if capture.capture_default_config {
        match tag.config() {
            Ok(config) => Some(config),
            Err(_) => {
                eprintln!("Could not read tag config: {}", tag.debug_message());
                return ExitCode::FAILURE;
            }
        }
    } else {
        None
    };
    let mut status = match tag.status() {
        Ok(status) => status,
        Err(_) => {
            eprintln!("Could not read tag status: {}", tag.debug_message());
            return ExitCode::FAILURE;
        }
    };
    let Some(voltage) = tag.voltage() else {
        eprintln!("Could not read tag voltage");
        return ExitCode::FAILURE;
    };

    if capture.sanitize {
        sanitize_info(&mut info, &capture.id);
    }
    sanitize_status(&mut status);

    if !write_fixture(&capture, &info, config.as_ref(), &status, voltage) {
        return ExitCode::FAILURE;
    }
    if capture.print_summary {
        print_summary(&capture, &info, &status, voltage);
    }
    ExitCode::SUCCESS
}

#[cfg(test)]
mod tests {
    use super::*;
    use tagcore::proto::config;

    #[test]
    fn json_matches_the_checked_in_fixture_layout() {
        let cfg = Config {
            tag_type: TagType::BittagLe as i32,
            active_interval: Some(config::Interval {
                start_epoch: 0,
                end_epoch: 2147483647,
            }),
            hibernate: vec![config::Interval::default(); 2],
            ..Default::default()
        };
        let json = message_to_json(&cfg);
        assert!(json.starts_with(
            "{\n \"tag_type\": \"BITTAG_LE\",\n \"active_interval\": {\n  \"start_epoch\": 0,\n  \"end_epoch\": 2147483647\n },\n \"hibernate\": [\n  {\n   \"start_epoch\": 0,\n"
        ));
        // period (7) follows bittag_log (4); unset messages are omitted.
        assert!(json.contains(
            "\"bittag_log\": \"BITTAG_UNSPECIFIED\",\n \"period\": 0,\n \"start_delay\": 0\n}\n"
        ));
        assert!(!json.contains("adxl362"));
    }

    #[test]
    fn optional_fields_print_only_when_set() {
        let mut info = TagInfo::default();
        assert!(!message_to_json(&info).contains("ppm_clock_error"));
        info.ppm_clock_error = Some(0.0);
        info.intflashsz = 256;
        let json = message_to_json(&info);
        assert!(json.contains("\"ppm_clock_error\": 0\n"));
        assert!(json.contains("\"intflashsz\": \"256\""));
    }

    #[test]
    fn helpers() {
        assert_eq!(lower_kebab("BITTAG_LE"), "bittag-le");
        assert_eq!(json_escape("a\"\n\u{1}"), "a\\\"\\n\\u0001");
        assert_eq!(base64(b"Man"), "TWFu");
        assert_eq!(base64(b"Ma"), "TWE=");
        assert_eq!(base64(b"M"), "TQ==");
    }
}
