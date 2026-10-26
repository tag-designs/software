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

/// Parses an address as `strtoul(text, nullptr, 0)` does: leading
/// whitespace, optional sign, `0x` hex or leading-`0` octal, stopping at the
/// first invalid character. Unparsable text yields 0, a negative value wraps
/// and an overflow saturates, then the result is truncated to 32 bits, as
/// the C++ cast does.
fn strtoul(text: &str) -> u32 {
    let s = text.trim_start();
    let (negative, s) = match s.as_bytes().first() {
        Some(b'-') => (true, &s[1..]),
        Some(b'+') => (false, &s[1..]),
        _ => (false, s),
    };
    let (radix, digits) = match s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")) {
        Some(hex) if hex.chars().next().is_some_and(|c| c.is_ascii_hexdigit()) => (16, hex),
        _ if s.starts_with('0') => (8, s),
        _ => (10, s),
    };
    let mut value: u64 = 0;
    let mut overflow = false;
    for c in digits.chars() {
        let Some(d) = c.to_digit(radix) else { break };
        match value
            .checked_mul(u64::from(radix))
            .and_then(|v| v.checked_add(u64::from(d)))
        {
            Some(v) => value = v,
            None => overflow = true,
        }
    }
    if overflow {
        value = u64::MAX;
    } else if negative {
        value = value.wrapping_neg();
    }
    value as u32
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
        let addr = strtoul(addr_str);
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
    use super::strtoul;

    #[test]
    fn addresses_parse_like_strtoul() {
        assert_eq!(strtoul("0xE000EDFC"), 0xE000_EDFC);
        assert_eq!(strtoul("536870912"), 0x2000_0000);
        assert_eq!(strtoul("010"), 8);
        assert_eq!(strtoul("12abc"), 12);
        assert_eq!(strtoul("zzz"), 0);
        assert_eq!(strtoul("-4"), 0xFFFF_FFFC);
        assert_eq!(strtoul("0x"), 0);
    }
}
