//! `tag-capture`: capture a tag's registers, internal flash, SRAM and
//! external flash over SWD without booting its firmware.
//!
//! Port of `host/commandline/tag-capture.cc` onto `tagcore::recovery`. The
//! first thing to run on a returned tag, before `tag-info` or any other
//! monitor tool: a monitor attach lets the firmware boot, which clears the
//! reset flags and can rewrite `pState` and the marker log.
//!
//! Writes `captures/capture-YYYYmmdd-HHMMSS/` (UTC) with one file per region
//! and a `manifest.json`: registers (the backup registers above all), option
//! bytes, OTP and internal flash; SRAM only with `--sram`; then the external
//! flash, through the loader the tag's identity record names, found under
//! `--loader-dir`, then `$TAG_LOADER_DIR`, then `build-host/embedded/loaders`;
//! or `--loader`. A firmware release carries its loaders under
//! `firmware/loaders/`, so for a returned tag point `TAG_LOADER_DIR` at the
//! unpacked release of the image it runs. `--no-external` skips it. For SPI NAND, blank blocks are
//! skipped (`--external-full` reads them).
//!
//! `--with-build DIR` stores the firmware build under `build/`: the ELF in
//! DIR (or DIR itself, when it names an ELF), its `.map`, `.list` and build
//! manifest, and any `--extra` files such as the target's `project.mk`. The
//! manifest records their hashes, the image's region symbols, and whether
//! the ELF matches the captured internal flash -- that is, whether it is the
//! image that was running. With `--sram` this holds everything
//! `tag_capture_state.py` stored.
//!
//! Without `--with-build` the capture directory is the same as the C++
//! tool's, file for file, so `decode_capture.py` and `tag-rebuild` read
//! either.
//!
//! # Exit status
//!
//! 0 complete, 2 written but some region failed, 1 no capture. Option
//! errors exit 2 (clap).
//!
//! See `host/libraries/tagcore/design/swd-recovery.md`.

use std::path::PathBuf;
use std::process::ExitCode;

use clap::Parser;
use tag_tools::{BaseArgs, init_logging, select_base_from};
use tagcore::recovery::{BuildFiles, CaptureOptions, ImageCheck, Session, SwdExit, capture_tag};

/// capture a tag's state over SWD without booting it
#[derive(Parser)]
#[command(name = "tag-capture")]
struct Cli {
    /// The shared `--debug` and `--base` options.
    #[command(flatten)]
    base: BaseArgs,

    /// Parent directory for the capture
    #[arg(short, long, default_value = "captures")]
    out_dir: PathBuf,

    /// Why the capture was taken, recorded in the manifest
    #[arg(short, long, default_value = "")]
    reason: String,

    /// Also capture SRAM (rarely useful: Shutdown and Standby lose it)
    #[arg(long)]
    sram: bool,

    /// Leave the core halted instead of resetting the tag
    #[arg(long)]
    leave_halted: bool,

    /// Skip the external flash
    #[arg(long)]
    no_external: bool,

    /// Loader image for the external flash (default: the one the identity
    /// record names)
    #[arg(long)]
    loader: Option<PathBuf>,

    /// Directory searched for <loader>.stldr (repeatable)
    #[arg(long = "loader-dir")]
    loader_dir: Vec<PathBuf>,

    /// SPI NAND: read every block, blank or not
    #[arg(long)]
    external_full: bool,

    /// Store the firmware build with the capture: a build directory holding
    /// one .elf, or the .elf itself
    #[arg(long, value_name = "DIR|ELF")]
    with_build: Option<PathBuf>,

    /// Another file to store under build/, such as project.mk (repeatable;
    /// needs --with-build)
    #[arg(long, value_name = "FILE", requires = "with_build")]
    extra: Vec<PathBuf>,
}

/// Parses options, selects a base, captures, and prints a summary.
fn main() -> ExitCode {
    tag_tools::restore_sigpipe();
    let cli = Cli::parse();
    init_logging(&cli.base);

    let mut loader_dirs = cli.loader_dir.clone();
    if let Some(env) = std::env::var_os("TAG_LOADER_DIR") {
        loader_dirs.push(env.into());
    }
    loader_dirs.push("build-host/embedded/loaders".into());

    // Checked before attaching, so a wrong path costs nothing on the tag.
    let build = match cli.with_build.as_deref().map(|b| BuildFiles::resolve(b, &cli.extra)) {
        None => None,
        Some(Ok(b)) => Some(b),
        Some(Err(e)) => {
            eprintln!("--with-build: {e}");
            return ExitCode::FAILURE;
        }
    };

    let mut print = |line: &str| println!("  {line}");
    let mut o = CaptureOptions {
        parent_dir: cli.out_dir.clone(),
        reason: cli.reason.clone(),
        include_sram: cli.sram,
        include_external: !cli.no_external,
        loader_path: cli.loader.clone(),
        loader_dirs,
        external_full: cli.external_full,
        exit: if cli.leave_halted { SwdExit::LeaveHalted } else { SwdExit::HardwareReset },
        build,
        progress: Some(&mut print),
    };

    let dev = select_base_from::<Cli>(Session::new().available(), &cli.base);
    let r = match capture_tag(&mut o, &dev) {
        Ok(r) => r,
        Err((e, a)) => {
            eprintln!("capture failed: {e}");
            if a.idcode != 0 {
                eprintln!(
                    "  halted={} pc=0x{:08X} reset_vector=0x{:08X}",
                    u8::from(a.halted),
                    a.pc,
                    a.reset_vector
                );
            }
            return ExitCode::FAILURE;
        }
    };

    println!("\n{}  uid {}  {:.2} V", r.mcu, r.uid, r.attach.voltage);
    println!("halted at reset vector 0x{:08X} (firmware did not run)", r.attach.pc);
    for g in &r.regions {
        let rate = if g.seconds > 0.0 { f64::from(g.size) / g.seconds } else { 0.0 };
        println!(
            "  {:<16} {:>8} bytes  {:<4} {:>6.2} s {:>8.0} B/s  {}...",
            g.name,
            g.size,
            if g.ok { "ok" } else { "FAIL" },
            g.seconds,
            rate,
            &g.sha256[..16.min(g.sha256.len())]
        );
        for (addr, len) in &g.failed {
            println!("      unreadable 0x{addr:08X} +{len}");
        }
    }
    if let Ok(id) = &r.identity {
        let sha = id.string("git_sha");
        println!(
            "  identity: {} on {}, loader {}, git {}",
            id.string("target"),
            id.string("board"),
            id.string("loader"),
            &sha[..8.min(sha.len())]
        );
    }
    if r.external_attempted {
        let x = &r.external;
        print!(
            "  external flash {}: JEDEC 0x{:06X}, {:.1} s",
            if x.ok { "ok" } else { "FAIL" },
            x.jedec,
            x.seconds
        );
        if x.paged {
            print!(
                ", {} blocks read, {} blank, {} uncorrectable pages",
                x.blocks_read, x.blocks_blank, x.pages_uncorrectable
            );
        }
        println!();
        if !x.error.is_empty() {
            println!("      {}", x.error);
        }
    }
    if !r.external_note.is_empty() {
        println!("  external flash: {}", r.external_note);
    }
    if let Some(b) = &r.build {
        let check = match &b.image_check {
            ImageCheck::Matches { bytes } => format!("matches the captured flash ({bytes} bytes)"),
            ImageCheck::Differs { first_difference, .. } => {
                format!("DIFFERS from the captured flash at 0x{first_difference:08X}")
            }
            ImageCheck::NotChecked(why) => format!("not checked: {why}"),
        };
        println!("  build: {} files stored, {} {check}", b.files.len(), b.elf);
        for e in &b.errors {
            println!("      {e}");
        }
    }
    println!(
        "{}\n{}",
        if r.complete { "complete" } else { "INCOMPLETE" },
        r.manifest.display()
    );
    if r.complete { ExitCode::SUCCESS } else { ExitCode::from(2) }
}
