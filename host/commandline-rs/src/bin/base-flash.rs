//! `base-flash`: program a base's firmware through its MCU's ROM bootloader
//! over USB DFU, without STM32CubeProgrammer.
//!
//! The base is put in DFU mode by plugging it into USB with its button held.
//! `base-flash program <elf>` then writes the image's flash contents through
//! the bootloader's DfuSe interface, as
//! `dfu-util -a 0 -i 0 -s 0x08000000:leave -D <bin>` would, and leaves DFU
//! mode so the base starts the new firmware. It replaces the CMake `-dfu`
//! targets' `STM32_Programmer_CLI -c port=usb1 -d <elf>`; the DfuSe details are
//! in `tagprobe::dfu`, the plan in
//! `host/commandline-rs/design/proposals/programming-without-cubeprogrammer.md`.
//!
//! Commands:
//!
//! - `list`: the devices in DFU mode, with the bootloader's serial.
//! - `program <elf> [--serial S] [--wait SECONDS]`: write and start the
//!   image. With `--wait` it waits that long for a DFU device to appear, so
//!   it can be started before the base is plugged in.
//!
//! After leaving DFU mode the base must come back as an ST-LINK within a few
//! seconds and report, through its identify command, the identity string the
//! image carries (`base=...;fw=<commit>;dirty=...;date=...`). Only then does
//! `program` exit 0. A base whose firmware answers no identify command, or
//! an image without an identity string, is reported as unconfirmed and exits
//! 1.
//!
//! Exits 0 on success, 1 on failure, 2 on bad options (clap).
//!
//! # Hardware side effects
//!
//! Erases and rewrites the internal flash pages the image covers, in the
//! base's MCU, never its option bytes. A tag on the base is not touched by
//! the DFU download, but the base's restart drops the tag's power and SWD.

use std::io::Write as _;
use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::thread::sleep;
use std::time::{Duration, Instant};

use clap::{Parser, Subcommand};
use log::LevelFilter;
use tag_tools::init_logging_at;
use tagprobe::dfu::{self, DfuDevice};

/// How often to look for a device while waiting.
const POLL: Duration = Duration::from_millis(250);
/// How long a base may take to come back as an ST-LINK after leaving DFU.
const RETURN_TIMEOUT: Duration = Duration::from_secs(10);

/// program a base's firmware through its ROM bootloader over USB DFU
#[derive(Parser)]
#[command(name = "base-flash")]
struct Cli {
    /// Set log level to DEBUG
    #[arg(short, long, global = true)]
    debug: bool,

    /// What to do.
    #[command(subcommand)]
    command: Command,
}

/// The `base-flash` commands.
#[derive(Subcommand)]
enum Command {
    /// List the devices in DFU mode
    List,
    /// Write a base image through DFU and start it
    Program {
        /// The base firmware's .elf
        #[arg(value_parser = tag_tools::parse_elf_path)]
        elf: PathBuf,
        /// The DFU device's serial, as `base-flash list` prints it; needed
        /// only when more than one device is in DFU mode
        #[arg(short, long)]
        serial: Option<String>,
        /// Seconds to wait for a device in DFU mode
        #[arg(short, long, default_value_t = 0)]
        wait: u64,
    },
}

/// Prints one line per device in DFU mode.
fn list() -> Result<(), String> {
    let devices = dfu::list();
    if devices.is_empty() {
        println!("No device is in DFU mode");
    }
    for d in devices {
        println!(
            "{}  bus {} address {}",
            if d.serial.is_empty() {
                "(no serial)"
            } else {
                &d.serial
            },
            d.bus,
            d.address
        );
    }
    Ok(())
}

/// Finds the DFU device to use, waiting up to `wait` for one to appear.
///
/// # Errors
///
/// A message when none appears in time, none has the serial, or several are
/// present and none was named.
fn find_device(serial: Option<&str>, wait: Duration) -> Result<DfuDevice, String> {
    let deadline = Instant::now() + wait;
    let mut told = false;
    loop {
        let devices = dfu::list();
        let chosen: Vec<_> = devices
            .iter()
            .filter(|d| serial.is_none_or(|s| d.serial.eq_ignore_ascii_case(s)))
            .cloned()
            .collect();
        match chosen.len() {
            1 => return Ok(chosen.into_iter().next().expect("one device")),
            0 if Instant::now() < deadline => {
                if !told {
                    println!("Waiting for a base in DFU mode: plug it in with its button held");
                    told = true;
                }
                sleep(POLL);
            }
            0 => {
                return Err(match serial {
                    Some(s) => format!("No device with serial {s} is in DFU mode"),
                    None => {
                        "No device is in DFU mode: plug the base in with its button held".into()
                    }
                });
            }
            n => {
                return Err(format!(
                    "{n} devices are in DFU mode; choose one with --serial:\n{}",
                    chosen
                        .iter()
                        .map(|d| format!("  {}", d.serial))
                        .collect::<Vec<_>>()
                        .join("\n")
                ));
            }
        }
    }
}

/// Returns the identity string the image carries
/// (`base=...;fw=...;dirty=...;date=...`), if any: the text a base running it
/// reports to the identify command.
fn image_identity(image: &[u8]) -> Option<String> {
    let at = image.windows(5).position(|w| w == b"base=")?;
    let end = image[at..]
        .iter()
        .position(|&b| b == 0 || !b.is_ascii_graphic())?;
    String::from_utf8(image[at..at + end].to_vec()).ok()
}

/// Waits for a base reporting `identity` to appear on USB as an ST-LINK.
///
/// # Errors
///
/// A message naming what was found instead when none does within
/// [`RETURN_TIMEOUT`].
fn confirm(identity: &str) -> Result<String, String> {
    let deadline = Instant::now() + RETURN_TIMEOUT;
    let mut seen = Vec::new();
    while Instant::now() < deadline {
        sleep(POLL);
        for base in tagprobe::list() {
            match tagprobe::identify(&base) {
                Some(found) if found == identity => return Ok(base.serial),
                Some(found) => seen.push(found),
                None => {}
            }
        }
    }
    seen.dedup();
    Err(if seen.is_empty() {
        format!(
            "no base reporting the new image came back within {} s",
            RETURN_TIMEOUT.as_secs()
        )
    } else {
        format!(
            "the bases on USB report {}, not the new image",
            seen.join(", ")
        )
    })
}

/// Writes `elf` through DFU, then confirms the base came back running it.
///
/// # Errors
///
/// A message when the image cannot be read, no DFU device is found, the
/// download fails, or the base does not come back reporting the image.
fn program(elf: &Path, serial: Option<&str>, wait: Duration) -> Result<(), String> {
    let (start, image) = dfu::flatten_elf(elf)?;
    let identity = image_identity(&image);
    println!(
        "{}: 0x{start:08x}-0x{:08x}, {} bytes",
        elf.display(),
        start as usize + image.len(),
        image.len()
    );
    if let Some(id) = &identity {
        println!("Image identity: {id}");
    }
    let device = find_device(serial, wait)?;
    println!(
        "Writing through DFU device {}",
        if device.serial.is_empty() {
            "(no serial)"
        } else {
            &device.serial
        }
    );
    dfu::program(&device, elf, |done, total| {
        let _ = write!(
            std::io::stderr(),
            "\rProgramming {:3}%",
            done * 100 / total.max(1)
        );
    })?;
    eprintln!();
    println!("Written; leaving DFU mode");
    let Some(identity) = identity else {
        return Err(
            "the image carries no identity string, so the result cannot be confirmed".into(),
        );
    };
    let base = confirm(&identity)?;
    println!("Confirmed: base {base} is running the new image");
    Ok(())
}

/// Parses the command line, runs the command and reports a failure on
/// stderr.
fn main() -> ExitCode {
    let cli = Cli::parse();
    init_logging_at(if cli.debug {
        LevelFilter::Debug
    } else {
        LevelFilter::Error
    });
    let result = match &cli.command {
        Command::List => list(),
        Command::Program { elf, serial, wait } => {
            program(elf, serial.as_deref(), Duration::from_secs(*wait))
        }
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("base-flash: {e}");
            ExitCode::FAILURE
        }
    }
}

/// Hardware-free checks.
#[cfg(test)]
mod tests {
    use super::*;

    /// Checks that the identity string is found and ends at its NUL.
    #[test]
    fn identity_is_found_in_the_image() {
        let image = b"\x00\x01base=bittag-base-v7;fw=be8f9062;dirty=0\x00more";
        assert_eq!(
            image_identity(image).as_deref(),
            Some("base=bittag-base-v7;fw=be8f9062;dirty=0")
        );
        assert_eq!(image_identity(b"no identity here"), None);
    }

    /// Checks that the command line is well formed.
    #[test]
    fn command_line_is_valid() {
        use clap::CommandFactory;
        Cli::command().debug_assert();
    }
}
