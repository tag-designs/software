//! `tag-monitor-test`: tests the monitor interface.
//!
//! Port of `host/commandline/tag-monitor-test.cc`. Drives the STM32L4
//! DebugMonitor handshake directly over [`LinkAdapt`], without tagcore's
//! `TagMonitor`, as an independent check of the interface: reset into the
//! monitor, read the version, SHA string and buffer size, send MONITORSTART,
//! print what was read and end the session with MONITORSTOP. It follows the
//! same rules as `TagMonitor`:
//!
//! - debug control is programmed before reset is released, because standby
//!   can power-cycle the core debug block (writes after that fault, ST-LINK
//!   status 0x81);
//! - DCRDR is both request and response mailbox, so a request is written
//!   only when MON_PEND and MON_REQ are clear, and a call waits for MON_REQ
//!   to clear before reading the result;
//! - C_MASKINTS is never left set, since it survives the halt release and
//!   stalls the firmware with every interrupt masked.
//!
//! STM32L4 only: on an STM32U3, whose monitor is the shared block, it says
//! so and exits before writing any debug register. Exits 0 once the
//! handshake completes, 1 otherwise.

use std::process::ExitCode;
use std::thread::sleep;
use std::time::Duration;

use clap::{CommandFactory, Parser};
use log::{debug, error, warn};
use tag_tools::{BaseArgs, exit_on_interrupt, init_logging, parse_bus_device};
use tagcore::monitor::abi::{command_word, info, op};
use tagcore::{LinkAdapt, UsbDev};

/// tests the monitor interface
#[derive(Parser)]
#[command(name = "tag-monitor-test")]
struct Cli {
    #[command(flatten)]
    base: BaseArgs,
}

const DHCSR: u32 = 0xE000_EDF0;
const DCRDR: u32 = 0xE000_EDF8;
const DEMCR: u32 = 0xE000_EDFC;

const DBGKEY: u32 = 0xA05F << 16;
const C_DEBUGEN: u32 = 1 << 0;
const C_HALT: u32 = 1 << 1;
const C_MASKINTS: u32 = 1 << 3;
const S_HALT: u32 = 1 << 17;

const VC_CORERESET: u32 = 1;
const MON_REQ: u32 = 1 << 19;
const MON_PEND: u32 = 1 << 17;
const MON_EN: u32 = 1 << 16;
const TRCENA: u32 = 1 << 24;

// DBGMCU ID code addresses, which tell the families apart.
const L4_DBGMCU_IDCODE: u32 = 0xE004_2000;
const U3_DBGMCU_IDCODE: u32 = 0xE004_4000;

/// True if `idcode` has a real device ID (neither all zero nor all one).
fn plausible_idcode(idcode: u32) -> bool {
    let device_id = idcode & 0xfff;
    device_id != 0 && device_id != 0xfff
}

fn ms(n: u64) -> Duration {
    Duration::from_millis(n)
}

/// The C++ `TestMonitor`: a `LinkAdapt` with its own monitor call and
/// attach sequence, and the state the attach reads.
struct TestMonitor {
    link: LinkAdapt,
    /// Monitor call buffer size the firmware reported.
    maxpacket: usize,
    /// The firmware's git SHA string, NUL padded.
    sha_str: [u8; 48],
    /// Monitor version the firmware reported.
    version: u32,
}

impl TestMonitor {
    fn new() -> Self {
        Self {
            link: LinkAdapt::new(),
            maxpacket: 0,
            sha_str: [0; 48],
            version: 0,
        }
    }

    /// The SHA string up to its first NUL.
    fn sha(&self) -> String {
        let end = self.sha_str.iter().position(|&b| b == 0).unwrap_or(48);
        String::from_utf8_lossy(&self.sha_str[..end]).into_owned()
    }

    /// Runs one monitor operation and returns its result word.
    ///
    /// MONITORSTOP is not waited for, since the tag may sleep as soon as it
    /// sees it, and returns 0. `None` if not attached, the monitor is still
    /// busy, a register access fails, or the handler does not answer within
    /// 2.5 s.
    fn call(&mut self, operation: u8, operand: i32) -> Option<u32> {
        const TIMEOUT_MS: u32 = 2500;
        if !self.link.is_attached() {
            error!("monitor not attached");
            return None;
        }
        let Some(demcr) = self.link.read_debug32(DEMCR) else {
            error!("DEMCR read failed");
            return None;
        };
        if demcr & (MON_PEND | MON_REQ) != 0 {
            error!("monitor busy: demcr=0x{demcr:x}");
            return None;
        }
        // VC_CORERESET is the attachment flag the firmware reads.
        if !(self
            .link
            .write_debug32(DCRDR, command_word(operation, operand))
            && self.link.write_debug32(
                DEMCR,
                demcr | TRCENA | MON_EN | MON_PEND | MON_REQ | VC_CORERESET,
            ))
        {
            error!("monitor request write failed");
            return None;
        }
        if operation == op::MONITORSTOP {
            sleep(ms(5));
            return Some(0);
        }

        let mut last = demcr;
        let mut answered = false;
        for _ in 0..TIMEOUT_MS {
            if let Some(d) = self.link.read_debug32(DEMCR) {
                last = d;
                if d & MON_REQ == 0 {
                    answered = true;
                    break;
                }
            }
            sleep(ms(1));
        }
        if !answered {
            error!("monitor call timed out op=0x{operation:x} operand={operand} demcr=0x{last:x}");
            return None;
        }
        let Some(result) = self.link.read_debug32(DCRDR) else {
            error!("monitor result read failed");
            return None;
        };
        debug!("operation = 0x{operation:x} operand = 0x{operand:x} result = 0x{result:x}");
        Some(result)
    }

    /// Resets the tag into its monitor and starts a session; on failure the
    /// link is closed.
    fn attach(&mut self, usbdev: &UsbDev) -> bool {
        if self.link.is_attached() {
            error!("Already attached");
            return false;
        }
        if !self.link.attach_with_reset(true, usbdev) {
            error!("Attach failed");
            return false;
        }
        sleep(ms(20));
        if self.handshake() {
            return true;
        }
        self.link.detach();
        false
    }

    /// The steps of [`attach`](Self::attach) after the probe is open, with
    /// the core held in reset.
    fn handshake(&mut self) -> bool {
        // L4 only. Identify the part while it is held in reset, as
        // TagMonitor does; on an STM32U3 the DebugMon bits below mean nothing
        // to the firmware, so refuse before writing any.
        let is_l4 = self
            .link
            .read_debug32(L4_DBGMCU_IDCODE)
            .is_some_and(plausible_idcode);
        if !is_l4
            && let Some(idcode) = self
                .link
                .read_debug32(U3_DBGMCU_IDCODE)
                .filter(|id| plausible_idcode(*id))
        {
            eprintln!(
                "tag-monitor-test drives the STM32L4 DebugMonitor only; this target is an \
                     STM32U3 (idcode 0x{idcode:x}). Use tag-info or tag-side-channel-check."
            );
            // Nothing was written: release reset so the tag boots as it
            // would at power-on.
            self.link.assert_reset(true);
            return false;
        }

        // Program debug control while the core is still held in reset.
        let Some(demcr) = self.link.read_debug32(DEMCR) else {
            error!("debug setup failed");
            return false;
        };
        if !(self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN)
            && self.link.write_debug32(
                DEMCR,
                (demcr | TRCENA | MON_EN | VC_CORERESET) & !(MON_REQ | MON_PEND),
            ))
        {
            error!("debug setup failed");
            return false;
        }
        if !self.link.assert_reset(true) {
            error!("reset release failed");
            return false;
        }
        sleep(ms(50));

        if let Some(dhcsr) = self.link.read_debug32(DHCSR) {
            debug!("read dhcsr 0x{dhcsr:x}");
            // Clear a C_MASKINTS another tool left set; only possible while
            // halted, in a write that keeps C_HALT.
            if dhcsr & C_MASKINTS != 0 && dhcsr & S_HALT != 0 {
                self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN | C_HALT);
            }
        }
        if !self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN) {
            warn!("halt release failed");
        }

        let Some(version) = self.call(op::TAG_MONITORINFO, info::MONITORVERSION) else {
            error!("couldn't fetch monitor version information");
            return false;
        };
        self.version = version;
        debug!("Monitor Version 0x{version:x}");

        let Some(sha_addr) = self.call(op::TAG_MONITORINFO, info::TAGSHASTR) else {
            error!("Couldn't find the address of the sha string");
            return false;
        };
        let mut sha = [0u8; 48];
        if !self.link.read_mem32(sha_addr, &mut sha) {
            error!("read_mem failed");
            return false;
        }
        self.sha_str = sha;
        debug!("Tag Hash String 0x{sha_addr:x}, {}", self.sha());

        let Some(size) = self.call(op::TAG_MONITORINFO, info::MONITORBUFSIZE) else {
            error!("couldn't fetch monitor buffer length");
            return false;
        };
        self.maxpacket = size as usize;

        if !matches!(self.call(op::MONITORSTART, 0), Some(s) if s != 0) {
            error!("Monitor Start failed");
            return false;
        }
        true
    }

    /// Ends the session with MONITORSTOP and releases the link. The firmware
    /// clears its own debug state on MONITORSTOP and may sleep at once, so
    /// nothing else is written.
    fn detach(&mut self) {
        if !self.link.is_attached() {
            return;
        }
        if self.call(op::MONITORSTOP, 0).is_none() {
            error!("Monitor Stop failed during detach");
        }
        self.link.detach();
    }
}

/// Finds the ST-LINK the arguments select, or exits: `options.cc`'s rule,
/// applied to [`LinkAdapt::available`] since this tool has no `Tag`.
fn select_device(link: &LinkAdapt, args: &BaseArgs) -> UsbDev {
    let devs = link.available();
    let device = args.base.clone().unwrap_or_default();
    let mut found = if devs.is_empty() { None } else { Some(0) };

    if devs.len() > 1 || !device.is_empty() {
        let Some((bus, address)) = parse_bus_device(&device) else {
            let _ = Cli::command().print_help();
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

fn main() -> ExitCode {
    let cli = Cli::parse();
    init_logging(&cli.base);

    let mut monitor = TestMonitor::new();
    let dev = select_device(&monitor.link, &cli.base);
    if !monitor.attach(&dev) {
        eprintln!("Attach failed");
        return ExitCode::FAILURE;
    }
    exit_on_interrupt();

    println!(
        "Monitor version 0x{:x}, buffer {} bytes, SHA {}",
        monitor.version,
        monitor.maxpacket,
        monitor.sha()
    );
    // End the session rather than leave it to the firmware's timeout.
    monitor.detach();
    ExitCode::SUCCESS
}
