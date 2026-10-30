//! Shared command-line handling for the Rust tag tools.
//!
//! Port of `host/commandline/options.cc` and the parts of `log.c` the tools
//! rely on. Every tool that talks to a tag flattens [`BaseArgs`] into its own
//! arguments, calls [`init_logging`], then [`select_base`] to find the
//! ST-LINK, exactly as the C++ tools call `parse_options()`:
//!
//! - `-d`/`--debug` logs at DEBUG; otherwise only errors are logged.
//! - `-b`/`--base bus:device` picks a base. It is required when more than
//!   one ST-LINK is connected; with none connected the tool exits.
//!
//! Log lines go to stderr as `HH:MM:SS LEVEL file:line: message`, the
//! format of `log.c`.
//!
//! Selecting a base touches no hardware beyond USB enumeration; it is the
//! tool's later `Tag::attach` that resets the tag.
//!
//! # Examples
//!
//! The skeleton every tool follows (`Cli` is the tool's own clap parser):
//!
//! ```no_run
//! use clap::Parser;
//! use tag_tools::{BaseArgs, init_logging, select_base};
//! use tagcore::Tag;
//!
//! #[derive(Parser)]
//! struct Cli {
//!     #[command(flatten)]
//!     base: BaseArgs,
//! }
//!
//! let cli = Cli::parse();
//! init_logging(&cli.base);
//! let tag = Tag::new();
//! let dev = select_base::<Cli>(&tag, &cli.base);
//! if !tag.attach(&dev) {
//!     std::process::exit(1);
//! }
//! ```

use std::io::Write as _;

use clap::{Args, CommandFactory};
use log::LevelFilter;
use tagcore::proto::TagState;
use tagcore::{Tag, UsbDev};

/// The options every tag tool shares (`options.cc`).
#[derive(Debug, Clone, Args)]
pub struct BaseArgs {
    /// Set log level to DEBUG
    #[arg(short, long)]
    pub debug: bool,

    /// Select bus:device
    #[arg(short, long, value_name = "BUS:DEVICE")]
    pub base: Option<String>,
}

/// Starts logging to stderr in the `log.c` format, at DEBUG with `--debug`
/// and at ERROR otherwise.
///
/// # Panics
///
/// Panics if a global logger is already installed, so call it once per
/// process.
pub fn init_logging(args: &BaseArgs) {
    init_logging_at(if args.debug {
        LevelFilter::Debug
    } else {
        LevelFilter::Error
    });
}

/// Starts logging to stderr in the `log.c` format at `level`.
///
/// Each line carries the local wall-clock time and the Rust source file and
/// line of the log call, or `?:0` when the record has none.
///
/// # Panics
///
/// Panics if a global logger is already installed.
pub fn init_logging_at(level: LevelFilter) {
    env_logger::Builder::new()
        .filter_level(level)
        // nusb logs every USB device's missing properties at DEBUG on each
        // enumeration, which buries the tools' own lines.
        .filter_module("nusb", level.min(LevelFilter::Warn))
        .format(|buf, record| {
            writeln!(
                buf,
                "{} {:<5} {}:{}: {}",
                chrono::Local::now().format("%H:%M:%S"),
                record.level(),
                record.file().unwrap_or("?"),
                record.line().unwrap_or(0),
                record.args()
            )
        })
        .init();
}

/// Changes the log level after [`init_logging`], as `log_set_level()` does.
///
/// Used by `tag-start` to silence the link errors expected once a started
/// tag drops the debug link.
pub fn set_log_level(level: LevelFilter) {
    log::set_max_level(level);
}

/// Exits with status 1 on Ctrl-C, as the C++ tools' SIGINT handler does.
///
/// The process exits from the handler thread without running destructors,
/// so no detach is sent to the base. A failure to install the handler
/// (one is already installed) is ignored, leaving the existing handler in
/// place.
pub fn exit_on_interrupt() {
    let _ = ctrlc::set_handler(|| std::process::exit(1));
}

/// Finds the ST-LINK the arguments select, or exits.
///
/// With one base connected and no `--base`, that base is used. Otherwise
/// `--base bus:device` must name one: a malformed value prints the help and
/// exits 1, and no match exits 1 with "No matching device". With no base
/// connected it exits 1 with "No matching device" too. `C` is the tool's
/// clap parser, whose help is printed.
///
/// Only USB enumeration happens here; nothing is sent to the base or tag.
pub fn select_base<C: CommandFactory>(tag: &Tag, args: &BaseArgs) -> UsbDev {
    let devs = tag.available();
    let device = args.base.clone().unwrap_or_default();
    let mut found = if devs.is_empty() { None } else { Some(0) };

    if devs.len() > 1 || !device.is_empty() {
        let Some((bus, address)) = parse_bus_device(&device) else {
            let _ = C::command().print_help();
            println!();
            std::process::exit(1);
        };
        found = devs
            .iter()
            .position(|d| d.bus == bus && d.address == address);
    }

    match found {
        Some(i) => devs[i].clone(),
        None => {
            eprintln!("No matching device {device}");
            std::process::exit(1);
        }
    }
}

/// Parses `bus:device` as `sscanf("%u:%u")` would, into USB bus and address.
///
/// Whitespace around the bus number is ignored, and the device number ends
/// at its first non-digit, as `%u` stops there. `None` if there is no `:`,
/// either number is missing or not decimal, or either does not fit a `u8`.
pub fn parse_bus_device(text: &str) -> Option<(u8, u8)> {
    let (bus, address) = text.split_once(':')?;
    let number = |s: &str| {
        s.trim()
            .parse::<u32>()
            .ok()
            .and_then(|n| u8::try_from(n).ok())
    };
    Some((
        number(bus)?,
        number(address.split(|c: char| !c.is_ascii_digit()).next()?)?,
    ))
}

/// The protobuf name of a tag state number, such as `"IDLE"`, or the number
/// itself when it has no name, as `TagState_Name()` prints it.
pub fn state_name(state: i32) -> String {
    TagState::try_from(state)
        .map(|s| s.as_str_name().to_owned())
        .unwrap_or_else(|_| state.to_string())
}

/// The protobuf name of an enum value of type `E`, or its number.
///
/// `name` is the enum's `as_str_name`, passed because prost generates it as
/// an inherent method rather than a trait method.
pub fn enum_name<E>(value: i32, name: fn(&E) -> &'static str) -> String
where
    E: TryFrom<i32>,
{
    E::try_from(value)
        .map(|e| name(&e).to_owned())
        .unwrap_or_else(|_| value.to_string())
}

/// Unit tests for argument parsing and state naming.
#[cfg(test)]
mod tests {
    use super::*;

    /// Checks `bus:device` parsing keeps `sscanf`'s leniency and rejects
    /// what `sscanf` would.
    #[test]
    fn bus_device_parses_like_sscanf() {
        assert_eq!(parse_bus_device("0:6"), Some((0, 6)));
        assert_eq!(parse_bus_device("20:3x"), Some((20, 3)));
        assert_eq!(parse_bus_device("6"), None);
        assert_eq!(parse_bus_device(""), None);
        assert_eq!(parse_bus_device("a:1"), None);
    }

    /// Checks a known state prints by name and an unknown one by number.
    #[test]
    fn state_names_fall_back_to_numbers() {
        assert_eq!(state_name(2), "IDLE");
        assert_eq!(state_name(99), "99");
    }
}
