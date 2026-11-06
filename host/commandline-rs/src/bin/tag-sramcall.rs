//! `tag-sramcall`: call one function of an SRAM-resident image on a halted
//! tag and print the transfer buffer.
//!
//! Ported from the deleted C++ `host/commandline/tag-sramcall.cc` onto `tagcore::recovery`.
//! Halts the tag at its reset vector (as `tag-capture` does), downloads an
//! image built with the loader framework into SRAM1, calls the named
//! function with the given arguments, and hex-dumps the first bytes of the
//! transfer buffer. An argument written `buf` is replaced by the buffer's
//! SRAM address. The tag is then reset and boots normally.
//!
//! For probes such as `embedded/loaders/RV3028_PresTagv3`, which reads the
//! RTC's registers without the tag's firmware:
//!
//! ```text
//! tag-sramcall --image RV3028_PresTagv3.elf \
//!     --call Rv3028ReadRegs --args buf,0,64 --dump 64
//! ```
//!
//! `--peek ADDR,LEN` also prints LEN bytes at ADDR, as 32-bit words, while
//! the core is still halted, for example an image's static diagnostics
//! found with `nm`.
//!
//! The image overwrites the start of SRAM1: run `tag-capture` first if SRAM
//! matters.
//!
//! # Exit status
//!
//! 0 the function returned 1, 2 it returned something else, 1 the call
//! could not be made. Option errors exit 2 (clap).

use std::path::PathBuf;
use std::process::ExitCode;
use std::time::Duration;

use clap::Parser;
use tag_tools::{BaseArgs, init_logging, select_base_from};
use tagcore::recovery::{Session, SramCall, SwdExit, TargetImage};

/// call a function of an SRAM image on a halted tag
#[derive(Parser)]
#[command(name = "tag-sramcall")]
struct Cli {
    /// The shared `--debug` and `--base` options.
    #[command(flatten)]
    base: BaseArgs,

    /// Image (.elf or .stldr)
    #[arg(short, long)]
    image: PathBuf,

    /// Function to call
    #[arg(short, long)]
    call: String,

    /// Comma-separated arguments; 'buf' is the buffer address
    #[arg(short, long, default_value = "")]
    args: String,

    /// Bytes of the buffer to print after the call
    #[arg(long, default_value_t = 0)]
    dump: u32,

    /// After the call, also print LEN bytes at ADDR (ADDR,LEN), for example
    /// an image's static diagnostics
    #[arg(long, default_value = "")]
    peek: String,

    /// Milliseconds to wait for the return
    #[arg(long, default_value_t = 2000)]
    timeout: u64,
}

/// Parses a number as `strtoul(text, nullptr, 0)` does: `0x` hex, a
/// leading-`0` octal, or decimal; text it cannot parse is 0, as there.
fn strtoul(text: &str) -> u32 {
    let t = text.trim();
    let (radix, digits) = match t.strip_prefix("0x").or_else(|| t.strip_prefix("0X")) {
        Some(h) => (16, h),
        None if t.len() > 1 && t.starts_with('0') => (8, &t[1..]),
        None => (10, t),
    };
    let end = digits.find(|c: char| !c.is_digit(radix)).unwrap_or(digits.len());
    u32::from_str_radix(&digits[..end], radix).unwrap_or(0)
}

/// Downloads, calls, prints, and returns the function's result.
fn run(s: &mut Session, cli: &Cli, image: &TargetImage) -> Result<u32, String> {
    let mut call = SramCall::download(s, image).map_err(|e| format!("download failed: {e}"))?;
    let dump = cli.dump.min(call.buffer_size());
    let mut args: Vec<u32> = cli
        .args
        .split(',')
        .filter(|t| !t.is_empty())
        .map(|t| if t == "buf" { call.buffer_address() } else { strtoul(t) })
        .collect();
    if args.len() > 4 {
        return Err("at most four arguments".into());
    }
    args.resize(4, 0);
    // Fill the buffer, so bytes the function did not write read as 0xFF.
    let words = ((dump + 3) & !3) as usize;
    if words > 0 {
        s.write(call.buffer_address(), &vec![0xFF; words]);
    }
    let r0 = call
        .call(s, &cli.call, &args, Duration::from_millis(cli.timeout))
        .map_err(|e| format!("call failed: {e}"))?;
    println!("{} returned {} (0x{r0:08X})", cli.call, r0 as i32);

    if dump > 0 {
        let mut buf = vec![0u8; words];
        if !s.read(call.buffer_address(), &mut buf, 4096, None) {
            return Err("SWD read of the buffer failed".into());
        }
        for (i, row) in buf[..dump as usize].chunks(16).enumerate() {
            let bytes: Vec<String> = row.iter().map(|b| format!(" {b:02X}")).collect();
            println!("  {:02X}:{}", i * 16, bytes.concat());
        }
    }
    if !cli.peek.is_empty() {
        let (addr, len) = match cli.peek.split_once(',') {
            Some((a, l)) => (strtoul(a), strtoul(l)),
            None => (strtoul(&cli.peek), 16),
        };
        let (addr, len) = (addr & !3, (len + 3) & !3);
        let mut buf = vec![0u8; len as usize];
        if s.read(addr, &mut buf, 4096, None) {
            println!("peek 0x{addr:08X}:");
            for (i, w) in buf.chunks(4).enumerate() {
                println!("  +{:02X}: 0x{:08X}", i * 4, u32::from_le_bytes(w.try_into().unwrap()));
            }
        } else {
            eprintln!("SWD read of the peek range failed");
        }
    }
    Ok(r0)
}

/// Parses options, makes the call and prints the result.
fn main() -> ExitCode {
    tag_tools::restore_sigpipe();
    let cli = Cli::parse();
    init_logging(&cli.base);

    let image = match TargetImage::load(&cli.image) {
        Ok(i) => i,
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
    let result = run(&mut s, &cli, &image);
    s.close(SwdExit::HardwareReset);
    match result {
        Ok(1) => ExitCode::SUCCESS,
        Ok(_) => ExitCode::from(2),
        Err(e) => {
            eprintln!("{e}");
            ExitCode::FAILURE
        }
    }
}
