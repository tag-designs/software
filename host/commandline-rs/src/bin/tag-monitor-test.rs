//! `tag-monitor-test`: tests the monitor interface.
//!
//! Port of `host/commandline/tag-monitor-test.cc`. A bench experiment rather
//! than a tool: it opens the ST-LINK directly (no `TagMonitor`), halts and
//! single-step-arms the core, pokes DCRDR/DEMCR with a few raw monitor
//! requests, then runs the L4 DebugMonitor handshake (version, SHA, buffer
//! size, MONITORSTART) with its own simplified call loop. It exits 0 once
//! attached, or 1 if the attach failed.
//!
//! It writes debug registers freely and leaves the session to the firmware's
//! timeout; do not point it at a tag mid-run.

use std::process::ExitCode;
use std::thread::sleep;
use std::time::Duration;

use clap::{CommandFactory, Parser};
use log::{debug, error};
use tag_tools::{BaseArgs, exit_on_interrupt, init_logging, parse_bus_device};
use tagcore::monitor_abi::{command_word, info, op};
use tagcore::{LinkAdapt, UsbDev};

/// tests the monitor interface
#[derive(Parser)]
#[command(name = "tag-monitor-test")]
struct Cli {
    #[command(flatten)]
    base: BaseArgs,
}

// ARM debug registers.
const DHCSR: u32 = 0xE000_EDF0;
const DCRDR: u32 = 0xE000_EDF8;
const DEMCR: u32 = 0xE000_EDFC;

const DBGKEY: u32 = 0xA05F << 16;
const C_DEBUGEN: u32 = 1 << 0;
const C_HALT: u32 = 1 << 1;
const C_STEP: u32 = 1 << 2;
const C_MASKINTS: u32 = 1 << 3;

const VC_CORERESET: u32 = 1;
const MON_REQ: u32 = 1 << 19;
const MON_PEND: u32 = 1 << 17;
const MON_EN: u32 = 1 << 16;
const TRCENA: u32 = 1 << 24;

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

    /// The SHA string up to its first NUL, as `%s` prints it.
    fn sha(&self) -> String {
        let end = self.sha_str.iter().position(|&b| b == 0).unwrap_or(48);
        String::from_utf8_lossy(&self.sha_str[..end]).into_owned()
    }

    /// Reads a debug register; a failed read leaves `*value` unchanged, as
    /// the C++ out-parameter does.
    fn read_debug(&mut self, addr: u32, value: &mut u32) -> bool {
        match self.link.read_debug32(addr) {
            Some(v) => {
                *value = v;
                true
            }
            None => false,
        }
    }

    /// Posts a DebugMonitor request and reads its result from DCRDR.
    ///
    /// Write and read failures are ignored, as in the C++. Its completion
    /// test is `~(demcr & MON_REQ)`, a bitwise NOT that is never zero, so
    /// the wait ends after the first DEMCR read and the timeout branch is
    /// unreachable; that is kept. A failed result read leaves `*result`
    /// unchanged.
    fn call(&mut self, operation: u8, operand: i32, result: Option<&mut u32>) -> bool {
        const TIMEOUT: u32 = 500;
        let mut demcr: u32 = 0;

        if !self.link.is_attached() {
            error!("monitor not attached");
            return false;
        }

        // write debug request data (operand,operation)
        self.link
            .write_debug32(DCRDR, command_word(operation, operand));
        sleep(ms(1));

        // Set MON_PEND, MON_REQ in DEMCR. VC_CORERESET is the attachment
        // flag the embedded app reads; it also halts the core at the reset
        // vector after a reset.
        self.link
            .write_debug32(DEMCR, TRCENA | MON_PEND | MON_REQ | MON_EN | VC_CORERESET);

        let mut i = 0;
        while i < TIMEOUT * 5 {
            self.read_debug(DEMCR, &mut demcr);
            if !(demcr & MON_REQ) != 0 {
                break;
            }
            sleep(ms(1));
            i += 1;
        }

        if i == TIMEOUT * 5 {
            error!("monitor call timed out\n");
            return false;
        }

        if let Some(result) = result {
            self.read_debug(DCRDR, result);
            debug!(
                "operation = 0x{operation:x} operand = 0x{operand:x} result = 0x{:x}\n",
                *result
            );
        }
        true
    }

    /// Opens the probe with reset held, halts and arms the core, issues three
    /// raw test requests, then runs the monitor handshake.
    fn attach(&mut self, usbdev: &UsbDev) -> bool {
        if self.link.is_attached() {
            error!("Already attached");
            return false;
        }

        if self.handshake(usbdev) {
            return true;
        }
        // try detach to clean up
        self.detach();
        false
    }

    /// The body of `attach`; false when it should clean up and fail.
    fn handshake(&mut self, usbdev: &UsbDev) -> bool {
        if !self.link.attach_with_reset(true, usbdev) {
            error!("Attach failed");
            return false;
        }
        sleep(ms(20));
        self.link.assert_reset(true);

        self.link.write_debug32(DHCSR, DBGKEY | C_HALT | C_DEBUGEN);
        sleep(ms(50));
        self.link
            .write_debug32(DHCSR, DBGKEY | C_MASKINTS | C_STEP | C_DEBUGEN);
        self.link.write_debug32(DEMCR, TRCENA | VC_CORERESET);
        self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN);

        let mut dhcsr = 0;
        self.read_debug(DHCSR, &mut dhcsr);
        debug!("read dhcsr 0x{dhcsr:x}\n");

        // do a simple test
        let mut tmp2 = 0;
        for (request, tag) in [(4u32, ""), (0x200, " (2)"), (0x300, " (3)")] {
            self.link.write_debug32(DCRDR, request);
            self.link.write_debug32(DEMCR, 0x10B_0001);
            self.read_debug(DEMCR, &mut tmp2);
            debug!("tmp = 0x{tmp2:x}\n");
            self.read_debug(DCRDR, &mut tmp2);
            debug!("tmp{tag} = 0x{tmp2:x}\n");
        }

        // Call monitor to get pointer to information block
        let mut version = self.version;
        if !self.call(
            op::TAG_MONITORINFO,
            info::MONITORVERSION,
            Some(&mut version),
        ) {
            error!("couldn't fetch monitor version information");
            self.link.detach();
            return false;
        }
        self.version = version;
        debug!("Monitor Version 0x{:x}", self.version);

        let mut tmp = 0u32;
        if !self.call(op::TAG_MONITORINFO, info::TAGSHASTR, Some(&mut tmp)) {
            // The C++ detaches here but carries on; the next call then fails
            // as not attached.
            error!("Couldn't find the address of the sha string");
            self.link.detach();
        } else {
            let mut sha = [0u8; 48];
            if !self.link.read_mem32(tmp, &mut sha) {
                error!("read_mem failed\n");
                self.link.detach();
                return false;
            }
            self.sha_str = sha;
        }
        debug!("Tag Hash String 0x{tmp:x}, {}\n", self.sha());

        if !self.call(op::TAG_MONITORINFO, info::MONITORBUFSIZE, Some(&mut tmp)) {
            error!("couldn't fetch monitor buffer length");
            self.link.detach();
            return false;
        }
        self.maxpacket = tmp as usize;

        // Uninitialised in the C++; zero here, so a failed read means failure.
        let mut success = 0u32;
        if !self.call(op::MONITORSTART, 0, Some(&mut success)) || success == 0 {
            error!("Monitor Start failed");
            self.link.detach();
            return false;
        }
        true
    }

    /// Clears the monitor bits in DEMCR and releases the probe; does nothing
    /// when not attached. Sends no MONITORSTOP.
    fn detach(&mut self) {
        if !self.link.is_attached() {
            return;
        }
        let mut demcr = 0;
        self.read_debug(DEMCR, &mut demcr);
        self.link
            .write_debug32(DEMCR, demcr & !(VC_CORERESET | MON_PEND | MON_REQ | MON_EN));
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
        // Exiting 0 here reported success from a tool that never reached
        // the tag, which a script cannot tell apart from a completed
        // operation.
        return ExitCode::FAILURE;
    }

    // catch ctl-c
    exit_on_interrupt();
    ExitCode::SUCCESS
}
