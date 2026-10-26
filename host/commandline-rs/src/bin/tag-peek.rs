//! `tag-peek`: read words of live tag memory or peripheral space.
//!
//! Port of `host/commandline/tag-peek.cc`. A diagnostic tool: reads one or
//! more words in a single attach, between monitor RPCs, without halting the
//! core (unlike `STM32_Programmer_CLI -r32`, which needs connect-under-reset).
//! Uses [`Tag::read_memory`], so addresses must be word aligned. Not for
//! shipping.

use std::process::ExitCode;

use clap::Parser;
use tag_tools::{BaseArgs, init_logging, select_base};
use tagcore::{MemAccess, Tag};

/// read one or more words of live tag memory/peripheral space in a single
/// attach (one attach/detach disturbs the tag less than several separate
/// invocations)
#[derive(Parser)]
#[command(name = "tag-peek")]
struct Cli {
    #[command(flatten)]
    base: BaseArgs,

    /// address(es) to read, hex (e.g. 0xE000EDFC) or decimal
    #[arg(long = "addr", value_name = "ADDR", num_args = 1..)]
    addr_opt: Vec<String>,

    /// address(es) to read, as positional arguments
    #[arg(value_name = "ADDR")]
    addr: Vec<String>,
}

/// Parses an address as `strtoull(text, &end, 0)` would, but only when the
/// whole argument is a number that fits in 32 bits: `0x` hex, leading-`0`
/// octal or decimal, no sign. `None` for anything else, rather than the
/// address 0 that `strtoul` returns for text it cannot parse.
fn parse_address(text: &str) -> Option<u32> {
    let (radix, digits) = match text.strip_prefix("0x").or_else(|| text.strip_prefix("0X")) {
        Some(hex) => (16, hex),
        None if text.len() > 1 && text.starts_with('0') => (8, &text[1..]),
        None => (10, text),
    };
    if digits.is_empty() || digits.starts_with(['+', '-']) {
        return None;
    }
    u32::from_str_radix(digits, radix).ok()
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    init_logging(&cli.base);

    let tag = Tag::new();
    let dev = select_base::<Cli>(&tag, &cli.base);
    let addrs: Vec<&String> = cli.addr_opt.iter().chain(&cli.addr).collect();
    if addrs.is_empty() {
        eprintln!("usage: tag-peek <addr> [addr...]");
        return ExitCode::FAILURE;
    }

    if !tag.attach(&dev) {
        eprintln!("Attach failed");
        return ExitCode::FAILURE;
    }

    let mut rc = ExitCode::SUCCESS;
    for addr_str in addrs {
        let Some(addr) = parse_address(addr_str) else {
            eprintln!("not an address: {addr_str}");
            rc = ExitCode::FAILURE;
            continue;
        };
        let mut value = [0u8; 4];
        let result = tag.read_memory(addr, &mut value);
        if result != MemAccess::Ok {
            eprintln!("read failed for {addr_str}: {}", result.name());
            rc = ExitCode::FAILURE;
            continue;
        }
        println!("0x{addr:08X}: 0x{:08X}", u32::from_le_bytes(value));
    }
    rc
}

#[cfg(test)]
mod tests {
    use super::parse_address;

    #[test]
    fn addresses_must_be_whole_32_bit_numbers() {
        assert_eq!(parse_address("0xE000EDFC"), Some(0xE000_EDFC));
        assert_eq!(parse_address("536870912"), Some(0x2000_0000));
        assert_eq!(parse_address("010"), Some(8));
        assert_eq!(parse_address("0"), Some(0));
        assert_eq!(parse_address("12abc"), None);
        assert_eq!(parse_address("zzz"), None);
        assert_eq!(parse_address("-4"), None);
        assert_eq!(parse_address("0x"), None);
        assert_eq!(parse_address("0x100000000"), None);
        assert_eq!(parse_address(""), None);
    }
}
