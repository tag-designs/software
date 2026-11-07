//! `tag-xflash`: read a tag's external flash over SWD through an
//! SRAM-resident loader, without booting the tag's firmware.
//!
//! Ported from the deleted C++ `host/commandline/tag-xflash.cc` onto `tagcore::recovery`.
//!
//! - `tag-xflash dump --loader <image.stldr> -o <file>` halts the tag at its
//!   reset vector (as `tag-capture` does), downloads the loader into SRAM1
//!   and streams the part to a file. A loader with a `Serve()` entry point
//!   is driven through it, which also reports the part's JEDEC ID and status
//!   register as found; any other `.stldr`, ST's own included, is driven
//!   through the STM32CubeProgrammer entry points `Init` and `Read`, and
//!   `--st` forces that path. The loader's own `StorageInfo` descriptor (the
//!   ELF segment at address 0) gives the part's base address and size.
//! - `tag-xflash nand --loader <image> -o <dir>` reads a paged part (SPI
//!   NAND) into an existing directory through the capture code: page 0 of
//!   each block raw, blank blocks skipped unless `--full`, every other block
//!   raw and through ECC. Writes `raw.bin`, `ecc.bin`, `pages.csv` and
//!   `summary.txt`.
//!
//! - `tag-xflash rescue-erase --loader <image-RW.stldr> (--capture <dir> |
//!   --no-capture)` is step 1 of a rescue: it erases every sector of a NOR
//!   part that is not blank and reads each one back blank
//!   (`tagcore::recovery::rescue`). It refuses a read-only loader, a paged
//!   part, a part the image's identity record does not name, and, unless
//!   `--no-capture` is given, a capture directory of a different tag
//!   (matched by chip UID). Steps 2 (`tag-flash` internal erase and
//!   reflash) and 3 (`tag-reset`) are run separately, so each can be
//!   watched; see `docs/bench/capturing-a-tag.md`, "Rescuing a tag".
//!
//! No code from internal flash runs, and internal flash is not written.
//! Downloading the loader overwrites the start of SRAM1: run `tag-capture`
//! first if SRAM matters.
//!
//! # Exit status
//!
//! 0 the whole range was read (or, for `rescue-erase`, the part is proved
//! blank), 1 otherwise. Option errors exit 2 (clap).

use std::fs::File;
use std::io::Write as _;
use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::time::{Duration, Instant};

use clap::{Parser, ValueEnum};
use tag_tools::{BaseArgs, init_logging, select_base_from};
use tagcore::recovery::{
    CaptureCheck, ExternalCaptureOptions, ExternalFlash, RescueProgress, Session, SramCall,
    SwdExit, TargetImage, capture_external_flash, rescue_erase,
};

/// Offset of the device name in the loader's `StorageInfo`
/// (`embedded/loaders/common/inc/dev_inf.h`, as laid out by
/// arm-none-eabi-gcc: `char[100]`, `u16`, then 32-bit fields).
const INFO_NAME: u32 = 0;
/// Offset of the part's base address in `StorageInfo`.
const INFO_START: u32 = 104;
/// Offset of the part's size in `StorageInfo`.
const INFO_SIZE: u32 = 108;
/// Longest a single `Init` call may take.
const INIT_TIMEOUT: Duration = Duration::from_millis(2000);
/// Longest a single `Read` call may take.
const READ_TIMEOUT: Duration = Duration::from_millis(5000);

/// What to read.
#[derive(Clone, Copy, ValueEnum)]
enum Command {
    /// Linear data into a file
    Dump,
    /// Whole pages, raw and through ECC, into the --out directory
    Nand,
    /// Rescue step 1: erase the external flash and prove it blank (NOR,
    /// -RW loader)
    RescueErase,
}

/// read a tag's external flash over SWD through a loader
#[derive(Parser)]
#[command(name = "tag-xflash")]
struct Cli {
    /// The shared `--debug` and `--base` options.
    #[command(flatten)]
    base: BaseArgs,

    /// dump (linear data), nand (whole pages, raw and ECC, into the --out
    /// directory) or rescue-erase
    command: Command,

    /// Loader image (.stldr or .elf)
    #[arg(short, long)]
    loader: PathBuf,

    /// Output file (dump) or existing directory (nand)
    #[arg(short, long, required_if_eq_any([("command", "dump"), ("command", "nand")]))]
    out: Option<PathBuf>,

    /// rescue-erase: a tag-capture directory of this tag (matched by chip
    /// UID), proving its evidence was kept
    #[arg(long, conflicts_with = "no_capture")]
    capture: Option<PathBuf>,

    /// rescue-erase: erase without a capture of the tag
    #[arg(long)]
    no_capture: bool,

    /// First byte of the part to read
    #[arg(long, default_value_t = 0, value_parser = parse_u32)]
    offset: u32,

    /// Bytes to read (default: to the end of the part)
    #[arg(long, value_parser = parse_u32)]
    length: Option<u32>,

    /// nand: read every block, blank or not
    #[arg(long)]
    full: bool,

    /// nand: first block to scan
    #[arg(long, default_value_t = 0, value_parser = parse_u32)]
    first_block: u32,

    /// nand: blocks to scan (default: to the end of the part)
    #[arg(long, default_value_t = 0, value_parser = parse_u32)]
    blocks: u32,

    /// Use the STM32CubeProgrammer entry points (Init, Read) even when the
    /// loader has Serve()
    #[arg(long)]
    st: bool,
}

/// Prints a rate line in place, every 256 KB and at the end.
struct Meter {
    /// When reading started.
    t0: Instant,
    /// Bytes at which to print next.
    next: u32,
}

impl Meter {
    /// A meter started now.
    fn new() -> Self {
        Meter { t0: Instant::now(), next: 0 }
    }

    /// Notes `done` of `total` bytes read.
    fn update(&mut self, done: u32, total: u32) {
        if done >= self.next || done == total {
            let secs = self.t0.elapsed().as_secs_f64();
            print!(
                "\r        {done} / {total} bytes  {:.0} B/s",
                if secs > 0.0 { f64::from(done) / secs } else { 0.0 }
            );
            let _ = std::io::stdout().flush();
            self.next = done + 256 * 1024;
        }
    }
}

/// Parses a numeric option, decimal or `0x` hex, as the C++ tool accepted.
///
/// # Errors
///
/// A message naming the text when it is not a number that fits in 32 bits.
fn parse_u32(text: &str) -> Result<u32, String> {
    let parsed = match text.strip_prefix("0x").or_else(|| text.strip_prefix("0X")) {
        Some(hex) => u32::from_str_radix(hex, 16),
        None => text.parse(),
    };
    parsed.map_err(|_| format!("{text} is not a number"))
}

/// Resolves the range to read: `offset` to the end of a part of `size`
/// bytes unless a length was given.
fn range(offset: u32, length: Option<u32>, size: u32) -> Result<u32, String> {
    let length = length.unwrap_or(size.saturating_sub(offset));
    if u64::from(offset) + u64::from(length) > u64::from(size) {
        return Err(format!("range runs past the part's {size} bytes"));
    }
    Ok(length)
}

/// Reads through the ST entry points `Init` and `Read`.
fn dump_st(s: &mut Session, loader: &TargetImage, cli: &Cli, out: &mut File) -> Result<(), String> {
    let word = |at: u32| loader.read_at(at, 4).map(|b| u32::from_le_bytes(b.try_into().unwrap()));
    let (Some(base), Some(size), Some(name)) =
        (word(INFO_START), word(INFO_SIZE), loader.read_at(INFO_NAME, 100))
    else {
        return Err(format!(
            "{} has no StorageInfo descriptor at address 0",
            loader.path().display()
        ));
    };
    let name = String::from_utf8_lossy(&name[..name.iter().position(|&b| b == 0).unwrap_or(100)])
        .into_owned();
    let length = range(cli.offset, cli.length, size)?;
    println!("loader  {}\n        {name}, {size} bytes at 0x{base:08X}", loader.path().display());

    let mut call = SramCall::download(s, loader).map_err(|e| format!("download failed: {e}"))?;
    println!(
        "        stack 0x{:08X}, buffer 0x{:08X} + {}",
        call.stack_top(),
        call.buffer_address(),
        call.buffer_size()
    );
    match call.call(s, "Init", &[], INIT_TIMEOUT) {
        Ok(1) => {}
        Ok(_) => return Err("Init failed (returned 0)".into()),
        Err(e) => return Err(format!("Init failed: {e}")),
    }
    let (bufaddr, bufsize) = (call.buffer_address(), call.buffer_size());
    let mut buf = vec![0u8; bufsize as usize];
    let mut meter = Meter::new();
    let mut done = 0u32;
    while done < length {
        let n = bufsize.min(length - done);
        let at = cli.offset + done;
        // Read(Address, Size, buffer): the address is the loader's fictional one.
        match call.call(s, "Read", &[base + at, n, bufaddr], READ_TIMEOUT) {
            Ok(1) => {}
            Ok(_) => return Err(format!("\nRead at offset {at} failed (returned 0)")),
            Err(e) => return Err(format!("\nRead at offset {at} failed: {e}")),
        }
        let words = ((n + 3) & !3) as usize;
        if !s.read(bufaddr, &mut buf[..words], 8192, None) {
            return Err(format!("\nSWD read of the buffer failed at offset {at}"));
        }
        out.write_all(&buf[..n as usize])
            .map_err(|_| "\nwrite to the output file failed".to_string())?;
        done += n;
        meter.update(done, length);
    }
    println!("\n        {length} bytes in {:.1} s", meter.t0.elapsed().as_secs_f64());
    Ok(())
}

/// Reads through the loader's `Serve()` entry point.
fn dump_serve(s: &mut Session, loader: &TargetImage, cli: &Cli, out: &mut File) -> Result<(), String> {
    let mut xf = ExternalFlash::open(s, loader).map_err(|e| format!("loader failed: {e}"))?;
    println!(
        "loader  {} (Serve)\n        JEDEC 0x{:06X}  SR1 0x{:02X}  {} bytes, {}-byte sectors, {}",
        loader.path().display(),
        xf.jedec(),
        xf.sr1(),
        xf.size(),
        xf.sector_size(),
        if xf.writable() { "read-write" } else { "read-only" }
    );
    let result = (|| {
        let length = range(cli.offset, cli.length, xf.size())?;
        let mut data = vec![0u8; length as usize];
        let mut meter = Meter::new();
        let mut report = |done, total| meter.update(done, total);
        xf.read(s, cli.offset, &mut data, Some(&mut report))
            .map_err(|e| format!("\nread failed: {e}"))?;
        println!("\n        {length} bytes in {:.1} s", meter.t0.elapsed().as_secs_f64());
        out.write_all(&data)
            .map_err(|_| "write to the output file failed".to_string())
    })();
    let closed = xf.close(s);
    result?;
    closed.map_err(|e| format!("Serve() did not exit cleanly: {e}"))
}

/// Reads a paged part into `dir` through the capture code, and writes
/// `summary.txt` beside the data.
fn dump_nand(s: &mut Session, loader: &TargetImage, cli: &Cli, dir: &Path) -> Result<(), String> {
    let mut show = |line: &str| {
        print!("\r        {line}   ");
        let _ = std::io::stdout().flush();
    };
    let mut o = ExternalCaptureOptions {
        prefix: String::new(),
        full: cli.full,
        first_block: cli.first_block,
        block_count: cli.blocks,
        progress: Some(&mut show),
    };
    let r = capture_external_flash(s, loader, dir, &mut o);
    println!(
        "\nloader  {} (Serve v{})\n        JEDEC 0x{:06X}  found A0={:02X} B0={:02X} C0={:02X} F0={:02X}",
        loader.path().display(),
        r.version,
        r.jedec,
        r.found & 0xFF,
        (r.found >> 8) & 0xFF,
        (r.found >> 16) & 0xFF,
        r.found >> 24
    );
    if !r.ok {
        return Err(r.error);
    }
    if !r.paged {
        return Err("this loader's part has no pages; use dump".into());
    }
    println!(
        "        {} blocks read ({} factory-marked), {} blank and skipped, {} uncorrectable pages, {:.0} s",
        r.blocks_read, r.blocks_marked, r.blocks_blank, r.pages_uncorrectable, r.seconds
    );
    let mut sum = format!(
        "loader {}\nloader_sha256 {}\nserve_version {}\njedec 0x{:06X}\nfound_A0 0x{:02X}\n\
         found_B0 0x{:02X}\nfound_C0 0x{:02X}\nfound_F0 0x{:02X}\npage_bytes {}\n\
         pages_per_block {}\nblocks_scanned {}\nfirst_block {}\nblocks_read {}\n\
         blocks_blank {}\nblocks_factory_marked {}\npages_uncorrectable {}\nfull {}\n",
        r.loader_path,
        r.loader_sha256,
        r.version,
        r.jedec,
        r.found & 0xFF,
        (r.found >> 8) & 0xFF,
        (r.found >> 16) & 0xFF,
        r.found >> 24,
        r.page_bytes,
        r.pages_per_block,
        r.blocks_scanned,
        cli.first_block,
        r.blocks_read,
        r.blocks_blank,
        r.blocks_marked,
        r.pages_uncorrectable,
        u8::from(cli.full)
    );
    for f in &r.files {
        sum += &format!("file {} {} {}\n", f.file, f.size, f.sha256);
    }
    // As the C++ tool, a summary that cannot be written is not a failure.
    let _ = std::fs::write(dir.join("summary.txt"), sum);
    Ok(())
}

/// The `--out` path, which clap requires for `dump` and `nand`.
fn out_path(cli: &Cli) -> &Path {
    cli.out.as_deref().expect("clap requires --out for dump and nand")
}

/// Rescue step 1: erases the external flash and proves it blank.
fn rescue(s: &mut Session, loader: &TargetImage, cli: &Cli) -> Result<(), String> {
    let check = match (&cli.capture, cli.no_capture) {
        (Some(dir), _) => CaptureCheck::Capture(dir),
        (None, true) => CaptureCheck::NoCapture,
        (None, false) => {
            return Err("rescue-erase needs --capture <dir> of this tag, or --no-capture".into());
        }
    };
    println!("loader  {} (Serve)", loader.path().display());
    let t0 = Instant::now();
    let mut next = 0u32;
    let mut show = |p: &RescueProgress| {
        if p.done >= next || p.done == p.total {
            print!(
                "\r        {} / {} bytes scanned, {} sectors erased, {:.0} s",
                p.done,
                p.total,
                p.erased,
                t0.elapsed().as_secs_f64()
            );
            let _ = std::io::stdout().flush();
            next = p.done + 128 * 1024;
        }
    };
    let r = rescue_erase(s, loader, check, Some(&mut show));
    println!();
    let r = r?;
    println!(
        "        tag {}  JEDEC 0x{:06X}  SR1 as found 0x{:02X}",
        r.uid, r.jedec, r.sr1
    );
    for n in &r.notes {
        println!("        note: {n}");
    }
    println!(
        "        {} of {} {}-byte sectors held data and were erased; all {} bytes read back blank in {:.1} s",
        r.sectors_erased, r.sectors_scanned, r.sector_size, r.size, r.seconds
    );
    println!(
        "        next: step 2, erase and reflash the internal flash (tag-flash); then step 3, tag-reset"
    );
    Ok(())
}

/// Parses options and runs the command.
fn main() -> ExitCode {
    tag_tools::restore_sigpipe();
    let cli = Cli::parse();
    init_logging(&cli.base);
    // Checked before attaching, so a missing option costs nothing on the tag.
    if matches!(cli.command, Command::RescueErase) && cli.capture.is_none() && !cli.no_capture {
        eprintln!("rescue-erase needs --capture <dir> of this tag, or --no-capture");
        return ExitCode::from(2);
    }

    let loader = match TargetImage::load(&cli.loader) {
        Ok(l) => l,
        Err(e) => {
            eprintln!("{e}");
            return ExitCode::FAILURE;
        }
    };
    let mut s = Session::new();
    let dev = select_base_from::<Cli>(s.available(), &cli.base);
    if let Err(e) = s.open(&dev) {
        eprintln!("could not halt the tag at its reset vector: {e}");
        return ExitCode::FAILURE;
    }
    println!(
        "{} halted at reset vector 0x{:08X} (firmware did not run)",
        s.mcu().map_or("?", |m| m.name),
        s.attach_info().pc
    );

    let result = match cli.command {
        Command::RescueErase => rescue(&mut s, &loader, &cli),
        Command::Nand => dump_nand(&mut s, &loader, &cli, out_path(&cli)),
        Command::Dump => match File::create(out_path(&cli)) {
            Err(_) => Err(format!("cannot write {}", out_path(&cli).display())),
            Ok(mut out) => {
                if !cli.st && loader.symbol("loaderService").is_some() {
                    dump_serve(&mut s, &loader, &cli, &mut out)
                } else {
                    dump_st(&mut s, &loader, &cli, &mut out)
                }
            }
        },
    };
    s.close(SwdExit::HardwareReset);
    match result {
        Ok(()) => {
            println!("complete");
            ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!("{e}");
            println!("FAILED");
            ExitCode::FAILURE
        }
    }
}
