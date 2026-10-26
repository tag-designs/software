//! probe-rs spike: can probe-rs 0.32 drive our tag bases, and what debug
//! state does it leave on a tag?
//!
//! An experiment, not a tool. It answers three questions from the probe-rs
//! evaluation (see README.md):
//!
//! 1. `list` and `raw`: does probe-rs enumerate our ST-LINK-compatible base,
//!    attach through it, read debug-port registers (DPIDR, CTRL/STAT) and
//!    read memory and debug registers?
//! 2. `session`: does attach-under-reset leave the core halted at its reset
//!    vector, and what do DHCSR, DEMCR and DBGMCU_CR hold after the session
//!    is dropped -- with probe-rs's stock STM32 sequence (`--sequence st`)
//!    and with ours, which writes nothing to DBGMCU and clears the core
//!    debug state on detach (`--sequence tag`)?
//! 3. `program` (later): erase, program and start an image in one session.
//!
//! Every attach here holds the tag in reset first, so the firmware reboots,
//! as with every tool in this tree. `raw` reads only; `raw --clear` writes
//! the debug registers back to their power-on values.

use std::sync::Arc;
use std::time::Duration;

use anyhow::{Context, Result, anyhow, bail};
use clap::{Parser, Subcommand, ValueEnum};
use probe_rs::architecture::arm::FullyQualifiedApAddress;
use probe_rs::architecture::arm::dp::{DpAddress, DpRegisterAddress};
use probe_rs::architecture::arm::sequences::ArmDebugSequence;
use probe_rs::config::{DebugSequence, Registry, TargetSelector};
use probe_rs::probe::list::Lister;
use probe_rs::probe::{DebugProbeInfo, Probe};
use probe_rs::{MemoryInterface, Permissions, RegisterValue};

#[derive(Parser)]
#[command(name = "probe-rs-spike", about = "probe-rs against the tag bases")]
struct Cli {
    /// ST-LINK serial; the first ST-LINK found when omitted
    #[arg(short, long, global = true)]
    serial: Option<String>,

    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// List the probes probe-rs finds
    List,
    /// Hold reset, attach without a session, read DP and debug registers,
    /// release reset
    Raw {
        /// Also write DHCSR, DEMCR and DBGMCU_CR back to their reset values
        #[arg(long)]
        clear: bool,
    },
    /// Attach under reset with a session, read the halted core, detach, then
    /// read the debug state the session left
    Session {
        /// probe-rs target name, e.g. STM32L432KCUx or STM32U375RGTx
        #[arg(long)]
        chip: String,
        /// Whose attach/detach sequence to use
        #[arg(long, value_enum, default_value_t = Seq::Tag)]
        sequence: Seq,
    },
}

#[derive(Clone, Copy, ValueEnum, PartialEq)]
enum Seq {
    /// probe-rs's built-in sequence for the chip (Stm32Armv7 / Stm32Armv8)
    St,
    /// Ours: the ARM default, which leaves DBGMCU alone and on detach writes
    /// DHCSR and DEMCR to zero
    Tag,
}

/// Our sequence: no overrides, so every step is the ARM default. Unlike the
/// STM32 sequences it writes nothing to DBGMCU on attach, and its
/// `debug_core_stop` clears DHCSR.C_DEBUGEN and DEMCR on detach.
#[derive(Debug)]
struct TagSequence;

impl ArmDebugSequence for TagSequence {}

const DHCSR: u64 = 0xE000_EDF0;
const DEMCR: u64 = 0xE000_EDFC;
const DBGKEY: u32 = 0xA05F << 16;

/// The registers that tell the two families apart and hold debug state.
struct Family {
    name: &'static str,
    idcode: u64,
    dbgmcu_cr: u64,
    uid: u64,
}

const L4: Family = Family {
    name: "STM32L4",
    idcode: 0xE004_2000,
    dbgmcu_cr: 0xE004_2004,
    uid: 0x1FFF_7590,
};

const U3: Family = Family {
    name: "STM32U3",
    idcode: 0xE004_4000,
    dbgmcu_cr: 0xE004_4004,
    uid: 0x0BFA_0700,
};

fn plausible(idcode: u32) -> bool {
    let dev = idcode & 0xfff;
    dev != 0 && dev != 0xfff
}

fn stlinks() -> Vec<DebugProbeInfo> {
    Lister::new()
        .list_all()
        .into_iter()
        .filter(|p| p.vendor_id == 0x0483)
        .collect()
}

fn open(serial: &Option<String>) -> Result<Probe> {
    let probes = stlinks();
    let info = match serial {
        Some(sn) => probes
            .iter()
            .find(|p| p.serial_number.as_deref() == Some(sn.as_str()))
            .ok_or_else(|| anyhow!("no ST-LINK with serial {sn}"))?,
        None => probes.first().ok_or_else(|| anyhow!("no ST-LINK found"))?,
    };
    println!(
        "probe: {} {:04x}:{:04x} serial {}",
        info.identifier,
        info.vendor_id,
        info.product_id,
        info.serial_number.as_deref().unwrap_or("-")
    );
    info.open().context("opening the probe")
}

fn cmd_list() {
    let probes = Lister::new().list_all();
    if probes.is_empty() {
        println!("no probes found");
    }
    for p in probes {
        println!(
            "{} {:04x}:{:04x} serial {}",
            p.identifier,
            p.vendor_id,
            p.product_id,
            p.serial_number.as_deref().unwrap_or("-")
        );
    }
}

/// Holds reset, attaches without a session, reports the DP and debug state,
/// optionally clears it, and releases reset.
fn cmd_raw(serial: &Option<String>, clear: bool) -> Result<()> {
    let mut probe = open(serial)?;
    probe.target_reset_assert().context("asserting nRST")?;
    probe
        .attach_to_unspecified()
        .context("attach_to_unspecified")?;
    let mut iface = probe
        .try_into_arm_debug_interface(Arc::new(TagSequence))
        .map_err(|(_, e)| anyhow!("ARM interface: {e}"))?;

    let dp = DpAddress::Default;
    let dpidr = iface.read_raw_dp_register(
        dp,
        DpRegisterAddress {
            address: 0,
            bank: None,
        },
    );
    let ctrl = iface.read_raw_dp_register(
        dp,
        DpRegisterAddress {
            address: 4,
            bank: Some(0),
        },
    );
    println!("DP  DPIDR     {}", show(&dpidr));
    println!("DP  CTRL/STAT {}", show(&ctrl));

    let result = (|| -> Result<()> {
        let mut mem = iface
            .memory_interface(&FullyQualifiedApAddress::v1_with_default_dp(0))
            .context("memory interface on AP0")?;
        let mut family = None;
        for f in [&L4, &U3] {
            if let Ok(id) = mem.read_word_32(f.idcode) {
                if plausible(id) {
                    family = Some(f);
                    println!("MCU {} idcode 0x{id:08x}", f.name);
                    break;
                }
            }
        }
        let f = family.ok_or_else(|| anyhow!("neither L4 nor U3 idcode read"))?;
        let mut uid = [0u32; 3];
        mem.read_32(f.uid, &mut uid)?;
        println!("UID {:08x} {:08x} {:08x}", uid[0], uid[1], uid[2]);
        report_debug_state(|a| Ok(mem.read_word_32(a)?), f)?;
        if clear {
            mem.write_word_32(f.dbgmcu_cr, 0)?;
            mem.write_word_32(DEMCR, 0)?;
            mem.write_word_32(DHCSR, DBGKEY)?;
            println!("cleared DBGMCU_CR, DEMCR and DHCSR.C_DEBUGEN; now:");
            report_debug_state(|a| Ok(mem.read_word_32(a)?), f)?;
        }
        Ok(())
    })();

    let mut probe = iface.close();
    probe.target_reset_deassert().context("releasing nRST")?;
    result
}

fn show(r: &Result<u32, probe_rs::architecture::arm::ArmError>) -> String {
    match r {
        Ok(v) => format!("0x{v:08x}"),
        Err(e) => format!("ERROR {e}"),
    }
}

/// Prints DHCSR, DEMCR and DBGMCU_CR, read through `read`.
fn report_debug_state(mut read: impl FnMut(u64) -> Result<u32>, f: &Family) -> Result<()> {
    let dhcsr = read(DHCSR)?;
    let demcr = read(DEMCR)?;
    let dbgmcu = read(f.dbgmcu_cr)?;
    println!(
        "DHCSR 0x{dhcsr:08x} (C_DEBUGEN={} C_HALT={} C_MASKINTS={} S_HALT={})",
        dhcsr & 1,
        (dhcsr >> 1) & 1,
        (dhcsr >> 3) & 1,
        (dhcsr >> 17) & 1
    );
    println!(
        "DEMCR 0x{demcr:08x} (VC_CORERESET={} MON_EN={} TRCENA={})",
        demcr & 1,
        (demcr >> 16) & 1,
        (demcr >> 24) & 1
    );
    println!(
        "DBGMCU_CR 0x{dbgmcu:08x} (DBG_STOP={} DBG_STANDBY={})",
        (dbgmcu >> 1) & 1,
        (dbgmcu >> 2) & 1
    );
    Ok(())
}

/// Attaches under reset with a session, reads the halted core and the debug
/// state during the session, drops it, then reports what was left.
fn cmd_session(serial: &Option<String>, chip: &str, seq: Seq) -> Result<()> {
    let registry = Registry::from_builtin_families();
    let mut target = registry
        .get_target_by_name(chip)
        .with_context(|| format!("probe-rs has no target {chip}"))?;
    let builtin = match &target.debug_sequence {
        DebugSequence::Arm(s) => format!("{s:?}"),
        _ => "non-ARM".into(),
    };
    if seq == Seq::Tag {
        target.debug_sequence = DebugSequence::Arm(Arc::new(TagSequence));
    }
    println!(
        "target {chip}: built-in sequence {builtin}; using {}",
        if seq == Seq::Tag {
            "TagSequence"
        } else {
            "built-in"
        }
    );

    let probe = open(serial)?;
    {
        let mut session = probe
            .attach_under_reset_with_registry(
                TargetSelector::Specified(target),
                Permissions::default(),
                &registry,
            )
            .context("attach_under_reset")?;
        let mut core = session.core(0)?;
        let halted = core.core_halted()?;
        let pc: u32 = core.read_core_reg(core.program_counter())?;
        let sp: u32 = core.read_core_reg(core.stack_pointer())?;
        let reset_vector = core.read_word_32(0x0800_0004)?;
        println!(
            "core halted={halted} pc=0x{pc:08x} sp=0x{sp:08x} reset vector=0x{reset_vector:08x}{}",
            if pc == reset_vector & !1 {
                " (at the reset vector)"
            } else {
                ""
            }
        );
        let regs = core.registers();
        let xpsr = regs
            .core_registers()
            .find(|r| r.name() == "XPSR")
            .map(|r| r.id());
        if let Some(id) = xpsr {
            let v: RegisterValue = core.read_core_reg(id)?;
            println!("core xPSR {v}");
        }
        let f = if core.read_word_32(L4.idcode).is_ok_and(plausible) {
            &L4
        } else {
            &U3
        };
        println!("during the session:");
        report_debug_state(|a| Ok(core.read_word_32(a)?), f)?;
        drop(core);
        // Drop runs the sequence's detach (debug_core_stop) and resumes the core.
    }
    println!("session dropped; reading what it left (a fresh raw attach):");
    std::thread::sleep(Duration::from_millis(200));
    cmd_raw(serial, false)
}

fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .with_writer(std::io::stderr)
        .with_ansi(false)
        .init();
    let cli = Cli::parse();
    match cli.command {
        Command::List => {
            cmd_list();
            Ok(())
        }
        Command::Raw { clear } => cmd_raw(&cli.serial, clear),
        Command::Session { chip, sequence } => cmd_session(&cli.serial, &chip, sequence),
    }
    .or_else(|e| bail!("{e:#}"))
}
