//! Arm Cortex-M debug and system-control registers (ARMv7-M / ARMv8-M), and
//! the core halt and register reads the timeout reports use.

use std::thread::sleep;

use log::error;

use super::{TagMonitor, ms};

/// On a monitor call timeout, halt the core to read its registers for the
/// log (`TAGCORE_HALT_ON_MONITOR_TIMEOUT`).
pub(super) const HALT_ON_MONITOR_TIMEOUT: bool = true;
/// Leave a core halted by that probe halted, for a debugger
/// (`TAGCORE_LEAVE_HALTED_ON_MONITOR_TIMEOUT`).
pub(super) const LEAVE_HALTED_ON_MONITOR_TIMEOUT: bool = false;

// ARMv7-M / ARMv8-M debug and system control registers.
pub const DHCSR: u32 = 0xE000_EDF0;
pub const DCRSR: u32 = 0xE000_EDF4;
pub const DCRDR: u32 = 0xE000_EDF8;
pub const DEMCR: u32 = 0xE000_EDFC;
pub const SCB_ICSR: u32 = 0xE000_ED04;
pub const SCB_VTOR: u32 = 0xE000_ED08;
pub const SCB_AIRCR: u32 = 0xE000_ED0C;
pub const SCB_SCR: u32 = 0xE000_ED10;
pub const SCB_SHPR1: u32 = 0xE000_ED18;
pub const SCB_SHPR2: u32 = 0xE000_ED1C;
pub const SCB_SHPR3: u32 = 0xE000_ED20;
pub const SCB_SHCSR: u32 = 0xE000_ED24;
pub const SCB_CFSR: u32 = 0xE000_ED28;
pub const SCB_HFSR: u32 = 0xE000_ED2C;
pub const SCB_DFSR: u32 = 0xE000_ED30;
pub const SCB_MMFAR: u32 = 0xE000_ED34;
pub const SCB_BFAR: u32 = 0xE000_ED38;
pub const SCB_AFSR: u32 = 0xE000_ED3C;
pub const NVIC_IABR0: u32 = 0xE000_E304;

pub const DBGKEY: u32 = 0xA05F << 16;
pub const C_DEBUGEN: u32 = 1 << 0;
pub const C_HALT: u32 = 1 << 1;
pub const C_MASKINTS: u32 = 1 << 3;
pub const S_REGRDY: u32 = 1 << 16;
pub const S_HALT: u32 = 1 << 17;
pub const S_LOCKUP: u32 = 1 << 19;
pub const S_RESET_ST: u32 = 1 << 25;
/// DEMCR vector catch on core reset; also the "host attached" flag the L4
/// firmware reads.
pub const VC_CORERESET: u32 = 1;
pub const MON_REQ: u32 = 1 << 19;
pub const MON_PEND: u32 = 1 << 17;
pub const MON_EN: u32 = 1 << 16;

/// Core registers read from a core halted for a timeout report.
#[derive(Default)]
pub(super) struct HaltProbe {
    /// The probe, not something else, halted the core.
    pub(super) halted_by_probe: bool,
    /// DHCSR after the halt request; S_HALT set if the core stopped.
    pub(super) halted_dhcsr: u32,
    pub(super) sp: u32,
    pub(super) lr: u32,
    pub(super) pc: u32,
    pub(super) xpsr: u32,
    pub(super) msp: u32,
    pub(super) psp: u32,
    pub(super) special: u32,
}

impl TagMonitor {
    /// Reads a core register of a halted core through DCRSR/DCRDR.
    fn read_core_register(&mut self, reg: u32) -> Option<u32> {
        for _ in 0..100 {
            if self
                .link
                .read_debug32(DHCSR)
                .is_some_and(|d| d & S_REGRDY != 0)
            {
                break;
            }
            sleep(ms(1));
        }
        if !self.link.write_debug32(DCRSR, reg) {
            return None;
        }
        for _ in 0..100 {
            if self
                .link
                .read_debug32(DHCSR)
                .is_some_and(|d| d & S_REGRDY != 0)
            {
                return self.link.read_debug32(DCRDR);
            }
            sleep(ms(1));
        }
        None
    }

    /// For a timeout report: unless the core is locked up, halts it and
    /// reads its registers. Does nothing when halting on timeout is off.
    pub(super) fn halt_for_diagnostics(&mut self, dhcsr_snapshot: u32) -> HaltProbe {
        let mut probe = HaltProbe::default();
        if !HALT_ON_MONITOR_TIMEOUT || dhcsr_snapshot & S_LOCKUP != 0 {
            return probe;
        }
        probe.halted_by_probe = dhcsr_snapshot & S_HALT == 0;
        if !self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN | C_HALT) {
            return probe;
        }
        for _ in 0..100 {
            if let Some(d) = self.link.read_debug32(DHCSR) {
                probe.halted_dhcsr = d;
                if d & S_HALT != 0 {
                    break;
                }
            }
            sleep(ms(1));
        }
        if probe.halted_dhcsr & S_HALT != 0 {
            let mut reg = |r| self.read_core_register(r).unwrap_or(0);
            probe.sp = reg(13);
            probe.lr = reg(14);
            probe.pc = reg(15);
            probe.xpsr = reg(16);
            probe.msp = reg(17);
            probe.psp = reg(18);
            probe.special = reg(20);
        }
        probe
    }

    /// Reads the 8-word exception frame at `sp`, if readable.
    pub(super) fn read_frame(&mut self, sp: u32) -> Option<[u32; 8]> {
        let mut bytes = [0u8; 32];
        if !self.link.read_mem32(sp, &mut bytes) {
            return None;
        }
        let mut frame = [0u32; 8];
        for (word, b) in frame.iter_mut().zip(bytes.chunks(4)) {
            *word = u32::from_le_bytes(b.try_into().unwrap());
        }
        Some(frame)
    }

    /// Clears DHCSR.C_MASKINTS on a core halted at its reset vector.
    ///
    /// C_MASKINTS survives a system reset and can change only while the core
    /// is halted, in a write that keeps C_HALT set; the write that releases
    /// the halt cannot clear it. An SWD tool that ran code with interrupts
    /// masked and did not clear it on exit leaves it set, and once an attach
    /// sets C_DEBUGEN the firmware stalls in its idle thread with every
    /// interrupt masked. Does nothing unless the bit is set and the core is
    /// halted, so an ordinary attach is unchanged.
    pub(super) fn clear_stale_mask_ints(&mut self) {
        if let Some(dhcsr) = self.link.read_debug32(DHCSR)
            && dhcsr & C_MASKINTS != 0
            && dhcsr & S_HALT != 0
        {
            error!(
                "Monitor attach: DHCSR.C_MASKINTS was left set (dhcsr=0x{dhcsr:x}); clearing it"
            );
            self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN | C_HALT);
        }
    }
}
