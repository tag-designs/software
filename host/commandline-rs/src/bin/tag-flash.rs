//! `tag-flash`: program, erase and read a tag's internal flash through a base,
//! without STM32CubeProgrammer.
//!
//! A command line over `tagprobe`, the probe-rs library Tag Programmer
//! programs with, replacing the STM32_Programmer_CLI calls of the CMake
//! `-download` and `-erase` targets and the bench scripts. The plan is in
//! `host/commandline-rs/design/proposals/programming-without-cubeprogrammer.md`.
//!
//! Commands:
//!
//! - `list`: the bases on USB, with serial, `bus:device` and identity.
//! - `info`: the connect check: device ID, part, flash size, supply voltage.
//! - `program <elf> [--erase plan|image|all]`: erase, program, verify and
//!   start the image in one session. `plan` (the default) erases what Tag
//!   Programmer's flash plan for the image says: on an STM32L432 every page
//!   below the calibration page, or the whole part when there is none; on an
//!   STM32U375 only the image's pages, since every region it saves is pinned.
//!   `image` erases only the pages the image covers, keeping the stored
//!   configuration and state, and is refused unless the image already on the
//!   tag keeps its persistent region at the same address
//!   ([`check_persistent_unmoved`]). `all` mass-erases first.
//!
//!   On an STM32L432 the persistent region starts at the first page after
//!   the code, so it moves whenever the image crosses a page boundary.
//!   Erasing only the image's pages then leaves whatever was on the new
//!   region's first page -- the old image's code, typically -- where the new
//!   firmware reads its stored configuration, and its first start aborts. On
//!   2026-11-04 a UIUCTag image 2 KB shorter than its predecessor did exactly
//!   that.
//! - `erase`: mass erase, programming nothing.
//! - `read <address> <length> -o <file>`: read memory to a file.
//!
//! `-b`/`--base <serial>` picks a base by its USB serial, as `list` prints it;
//! it may be left out when exactly one base is connected. Unlike the monitor
//! tools' `-b bus:device`, a serial survives replugging.
//!
//! Exits 0 on success, 1 on failure, 2 on bad options (clap).
//!
//! # Hardware side effects
//!
//! Every command but `list` attaches **under reset**, so whatever the tag was
//! doing ends; the tag boots its firmware again afterwards. `program`
//! rewrites internal flash and starts the new image with a debugger jump to
//! its vector table, clearing FLASH_SR.PEMPTY on an STM32L4 first, so the tag
//! runs its image rather than its ROM bootloader. `erase` leaves the flash
//! empty: an STM32L4 then boots its ROM bootloader until it is programmed.
//! Neither touches the external flash log; per the flashing protocol in
//! `AGENTS.md`, erase that with `tag-reset` before flashing a new image.

use std::io::Write as _;
use std::path::PathBuf;
use std::process::ExitCode;

use clap::{Parser, Subcommand, ValueEnum};
use log::LevelFilter;
use tag_tools::init_logging_at;
use tagcore::UsbDev;
use tagprobe::{Driver, Erase, Progress};
use tagprogram_core::model::elf::Elf;
use tagprogram_core::model::flashplan::{self, plan_flash};
use tagprogram_core::model::identity::find_identity_record;

/// How long to wait after a command that attaches before exiting, so the
/// next tool can attach to the base.
const SETTLE: std::time::Duration = std::time::Duration::from_secs(1);

/// Start of internal flash on both supported MCUs.
const FLASH_BASE: u64 = 0x0800_0000;

/// Bytes read from [`FLASH_BASE`] to find the tag's identity record. The
/// record starts at `0x1A0` (STM32L432) or `0x240` (STM32U375) and is well
/// under 3 KB long.
const RECORD_READ: usize = 4096;

/// program, erase and read a tag's flash through a base
#[derive(Parser)]
#[command(name = "tag-flash")]
struct Cli {
    /// Set log level to DEBUG
    #[arg(short, long, global = true)]
    debug: bool,

    /// The base's USB serial, as `tag-flash list` prints it; needed only
    /// when more than one base is connected
    #[arg(short, long, global = true, value_name = "SERIAL")]
    base: Option<String>,

    /// What to do.
    #[command(subcommand)]
    command: Command,
}

/// The `tag-flash` commands.
#[derive(Subcommand)]
enum Command {
    /// List the bases on USB
    List,
    /// Check the connection: device, flash size and supply voltage
    Info,
    /// Program an image, verify it and start it
    Program {
        /// The firmware's .elf
        #[arg(value_parser = tag_tools::parse_elf_path)]
        elf: PathBuf,
        /// What to erase before programming
        #[arg(long, value_enum, default_value_t = EraseArg::Plan)]
        erase: EraseArg,
    },
    /// Mass-erase internal flash, programming nothing
    Erase,
    /// Read memory to a file
    Read {
        /// Start address (0x-prefixed hex or decimal)
        #[arg(value_parser = parse_number)]
        address: u64,
        /// Bytes to read (0x-prefixed hex or decimal)
        #[arg(value_parser = parse_number)]
        length: u64,
        /// File to write
        #[arg(short, long)]
        output: PathBuf,
    },
}

/// The `--erase` choices for `program`.
#[derive(Clone, Copy, ValueEnum)]
enum EraseArg {
    /// Only the pages the image covers, keeping stored configuration; refused
    /// if the persistent region would move
    Image,
    /// What Tag Programmer's flash plan for the image says: everything but
    /// the calibration page on an STM32L432
    Plan,
    /// The whole of internal flash
    All,
}

/// Parses a number written in decimal or with a `0x` prefix in hex.
///
/// # Errors
///
/// A message when the text is neither.
fn parse_number(text: &str) -> Result<u64, String> {
    let parsed = match text.strip_prefix("0x").or_else(|| text.strip_prefix("0X")) {
        Some(hex) => u64::from_str_radix(hex, 16),
        None => text.parse(),
    };
    parsed.map_err(|_| format!("{text} is not a number"))
}

/// Finds the base to use: the one whose serial is `wanted`, or the only one
/// connected when `wanted` is `None`.
///
/// # Errors
///
/// A message when no base is on USB, none has the serial, or several are
/// connected and none was named (the message lists them).
fn find_base(wanted: Option<&str>) -> Result<UsbDev, String> {
    let bases = tagprobe::list();
    match wanted {
        Some(serial) => bases
            .into_iter()
            .find(|b| b.serial.eq_ignore_ascii_case(serial))
            .ok_or_else(|| format!("No base with serial {serial} is on USB")),
        None => match bases.len() {
            0 => Err("No base is on USB".into()),
            1 => Ok(bases.into_iter().next().expect("one base")),
            _ => Err(format!(
                "{} bases are on USB; choose one with --base:\n{}",
                bases.len(),
                bases
                    .iter()
                    .map(|b| format!("  {}", b.serial))
                    .collect::<Vec<_>>()
                    .join("\n")
            )),
        },
    }
}

/// The base, its driver, the connect check and the probe-rs target: what
/// every command that attaches needs first.
struct Target {
    /// The base on USB.
    base: UsbDev,
    /// The probe-rs driver its identity selects.
    driver: Driver,
    /// The probe-rs target name for the part.
    chip: &'static str,
}

/// Finds the base, runs the connect check (an attach under reset) and picks
/// the probe-rs target from the part's device ID and flash size, falling back
/// to `image_flash` (the size the image was linked for) when the part does
/// not report one.
///
/// # Errors
///
/// A message when the base is not found, the connect check fails, or no
/// probe-rs target fits the part.
fn connect(wanted: Option<&str>, image_flash: Option<u32>) -> Result<Target, String> {
    let base = find_base(wanted)?;
    let driver = Driver::for_identity(tagprobe::identify(&base).as_deref());
    let found = tagprobe::connect_check(&base)?;
    let flash = tagprobe::flash_bytes(&base, driver, found.device_id)?;
    let chip = tagprobe::chip_for(found.device_id, flash.or(image_flash))?;
    log::debug!("{chip} through {} ({})", base.serial, driver.name());
    Ok(Target { base, driver, chip })
}

/// Prints one line per base: serial, `bus:device` and identity.
fn list() -> Result<(), String> {
    let bases = tagprobe::list();
    if bases.is_empty() {
        println!("No base is on USB");
    }
    for b in bases {
        let identity = tagprobe::identify(&b);
        println!(
            "{}  {}:{}  {}",
            if b.serial.is_empty() {
                "(no serial)"
            } else {
                &b.serial
            },
            b.bus,
            b.address,
            identity
                .as_deref()
                .unwrap_or("(no identity: older base firmware)")
        );
    }
    Ok(())
}

/// Runs the connect check and prints what it found.
///
/// # Errors
///
/// A message when the base is not found or the connect check fails.
fn info(wanted: Option<&str>) -> Result<(), String> {
    let base = find_base(wanted)?;
    let identity = tagprobe::identify(&base);
    let found = tagprobe::connect_check(&base)?;
    println!("Base:     {}", base.serial);
    println!("Identity: {}", identity.as_deref().unwrap_or("(none)"));
    println!(
        "Driver:   {}",
        Driver::for_identity(identity.as_deref()).name()
    );
    println!("Device:   0x{:03x} {}", found.device_id, found.device_name);
    let driver = Driver::for_identity(identity.as_deref());
    if let Some(bytes) = tagprobe::flash_bytes(&base, driver, found.device_id)? {
        println!("Flash:    {} KB", bytes / 1024);
    }
    match found.voltage {
        Some(v) => println!("Voltage:  {v:.2} V"),
        None => println!("Voltage:  (not read)"),
    }
    Ok(())
}

/// Turns `--erase` into the `tagprobe` erase for `elf`, printing what will
/// be erased.
///
/// # Errors
///
/// For `plan`, the flash plan's message when the image's layout cannot be
/// read or is unknown.
fn erase_for(elf: &std::path::Path, erase: EraseArg) -> Result<Erase, String> {
    Ok(match erase {
        EraseArg::Image => Erase::ImagePages,
        EraseArg::All => {
            println!("Mass erase: calibration stored in internal flash is destroyed.");
            Erase::All
        }
        EraseArg::Plan => {
            let plan = plan_flash(elf);
            println!("{}", plan.summary());
            match plan.erase {
                flashplan::Erase::Invalid => return Err(plan.error),
                flashplan::Erase::None => Erase::ImagePages,
                flashplan::Erase::All => Erase::All,
                flashplan::Erase::BelowCalibration => Erase::Range {
                    start: 0x0800_0000,
                    end: 0x0800_0000
                        + (plan.last_erased_page as u64 + 1) * u64::from(plan.page_size),
                },
            }
        }
    })
}

/// Refuses an erase of the image's pages only when the persistent region
/// would move.
///
/// Reads the identity record of the image already on the tag (an attach
/// under reset) and compares its persistent region's address with the new
/// image's `__persistent_start__`. Programming the image's pages leaves the
/// rest of flash as it was, which is right only when the new firmware will
/// find its stored configuration and state where the old one left them.
///
/// Only an explicit `--erase image` is checked. The plan for an STM32U375
/// image also erases the image's pages only, but there every saved region is
/// pinned, and a check would refuse a blank board's first programming.
///
/// # Errors
///
/// A message naming both addresses when they differ; when the new image has
/// no `__persistent_start__`; when the tag's flash holds no readable record
/// (an erased part, or firmware built before identity records) or a record
/// with no persistent region; or when the read fails.
fn check_persistent_unmoved(
    target: &Target,
    image: &Elf,
    elf: &std::path::Path,
) -> Result<(), String> {
    let new = image.symbol("__persistent_start__").ok_or_else(|| {
        format!(
            "{} has no __persistent_start__ symbol, so an erase of its pages \
             only cannot be checked; use --erase plan",
            elf.display()
        )
    })?;
    let flash = tagprobe::read_memory(
        &target.base,
        target.driver,
        target.chip,
        FLASH_BASE,
        RECORD_READ,
    )?;
    let old = find_identity_record(&flash)
        .map_err(|e| format!("tag flash: {e}"))
        .and_then(|r| {
            r.persistent_start
                .ok_or_else(|| "tag flash: identity record has no persistent region".into())
        })
        .map_err(|e: String| {
            format!(
                "{e}, so where the tag's image keeps its persistent region is \
                 unknown and --erase image could leave stale bytes there; \
                 use --erase plan"
            )
        })?;
    if old != new {
        return Err(format!(
            "The persistent region moves: the image on the tag keeps it at \
             0x{old:08x}, {} at 0x{new:08x}. --erase image would leave the old \
             bytes where the new firmware reads its stored configuration, and \
             its first start would abort. Use --erase plan (the default).",
            elf.display()
        ));
    }
    log::debug!("persistent region stays at 0x{new:08x}");
    Ok(())
}

/// Programs `elf`, verifies it and starts it, showing progress on stderr.
///
/// # Errors
///
/// A message when the image cannot be read, the erase plan is invalid, an
/// erase of the image's pages only would move the persistent region
/// ([`check_persistent_unmoved`]), or the attach, erase, programming,
/// verification or start fails.
fn program(wanted: Option<&str>, elf: &std::path::Path, erase: EraseArg) -> Result<(), String> {
    let image = Elf::load(elf).map_err(|e| format!("{}: {e}", elf.display()))?;
    let image_only = matches!(erase, EraseArg::Image);
    let erase = erase_for(elf, erase)?;
    let target = connect(wanted, image.symbol("__flash0_size__"))?;
    if image_only {
        check_persistent_unmoved(&target, &image, elf)?;
    }
    println!("Programming {} into {}", elf.display(), target.chip);
    let mut stderr = std::io::stderr();
    let mut step = "";
    tagprobe::program(
        &target.base,
        target.driver,
        target.chip,
        elf,
        erase,
        |p| match p {
            Progress::Step(name) => {
                step = name;
                let _ = write!(stderr, "\n{name}");
            }
            Progress::Bytes { done, total } if total > 0 => {
                let _ = write!(stderr, "\r{step} {:3}%", done * 100 / total);
            }
            Progress::Bytes { .. } => {}
        },
    )?;
    eprintln!();
    println!("Programmed, verified and started");
    Ok(())
}

/// Mass-erases the tag's internal flash.
///
/// # Errors
///
/// A message when the attach or the erase fails.
fn erase(wanted: Option<&str>) -> Result<(), String> {
    let target = connect(wanted, None)?;
    tagprobe::erase_all(&target.base, target.driver, target.chip)?;
    println!(
        "Erased. The flash is empty: an STM32L4 boots its ROM bootloader until it is programmed."
    );
    Ok(())
}

/// Reads `length` bytes at `address` into `output`.
///
/// # Errors
///
/// A message when the attach or the read fails, or the file cannot be
/// written.
fn read(
    wanted: Option<&str>,
    address: u64,
    length: u64,
    output: &std::path::Path,
) -> Result<(), String> {
    let length = usize::try_from(length).map_err(|_| format!("{length} bytes is too many"))?;
    let target = connect(wanted, None)?;
    let data = tagprobe::read_memory(&target.base, target.driver, target.chip, address, length)?;
    std::fs::write(output, &data).map_err(|e| format!("{}: {e}", output.display()))?;
    println!(
        "Read {} bytes at 0x{address:08x} into {}",
        data.len(),
        output.display()
    );
    Ok(())
}

/// Parses the command line, runs the command and reports a failure on
/// stderr.
fn main() -> ExitCode {
    tag_tools::restore_sigpipe();
    let cli = Cli::parse();
    init_logging_at(if cli.debug {
        LevelFilter::Debug
    } else {
        LevelFilter::Error
    });
    let base = cli.base.as_deref();
    let attaches = !matches!(cli.command, Command::List);
    let result = match &cli.command {
        Command::List => list(),
        Command::Info => info(base),
        Command::Program { elf, erase } => program(base, elf, *erase),
        Command::Erase => erase(base),
        Command::Read {
            address,
            length,
            output,
        } => read(base, *address, *length, output),
    };
    if attaches {
        // A tool that attaches the moment this one has let the base go can
        // fail its first debug read (SWD status 0x81); a second later it
        // does not. On a BitTag, 3 of 5 back-to-back `tag-info` runs failed
        // after `program`, and 6 of 6 passed with this pause.
        std::thread::sleep(SETTLE);
    }
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("tag-flash: {e}");
            ExitCode::FAILURE
        }
    }
}

/// Hardware-free checks of the argument parsing.
#[cfg(test)]
mod tests {
    use super::*;

    /// Checks that addresses parse in hex and decimal and junk is refused.
    #[test]
    fn numbers_parse_in_hex_and_decimal() {
        assert_eq!(parse_number("0x2003E000"), Ok(0x2003_E000));
        assert_eq!(parse_number("8192"), Ok(8192));
        assert!(parse_number("0xZZ").is_err());
        assert!(parse_number("ten").is_err());
    }

    /// Checks that the command line is well formed.
    #[test]
    fn command_line_is_valid() {
        use clap::CommandFactory;
        Cli::command().debug_assert();
    }
}
