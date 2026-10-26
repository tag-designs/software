//! Arm Cortex-M debug and system-control registers (ARMv7-M / ARMv8-M), and
//! the core halt and register reads the timeout reports use.
//!
//! The addresses are architectural (the System Control Space at
//! 0xE000_E000), the same on the STM32L4 (Cortex-M4, ARMv7-M) and the
//! STM32U3 (Cortex-M33, ARMv8-M). They are reached through
//! [`crate::LinkAdapt::read_debug32`] / [`crate::LinkAdapt::write_debug32`].
//! See the ARMv7-M Architecture Reference Manual, C1.6 (debug registers)
//! and B3.2 (SCB).
//!
//! The helpers here **halt the core**. They exist for monitor-timeout
//! reports: when a call does not complete, the host halts the core, reads
//! PC, LR, the stack pointers and the stacked exception frame so the log
//! says where the firmware was, and then resumes it (unless
//! `LEAVE_HALTED_ON_MONITOR_TIMEOUT` is set). The flags below mirror the
//! C++ compile-time switches of the same names.

use std::thread::sleep;

use log::error;

use super::{TagMonitor, ms};

/// On a monitor call timeout, halt the core to read its registers for the
/// log (`TAGCORE_HALT_ON_MONITOR_TIMEOUT`).
///
/// On by default, as in C++: the register dump is often the only evidence
/// of where the firmware hung.
pub(super) const HALT_ON_MONITOR_TIMEOUT: bool = true;
/// Leave a core halted by that probe halted, for a debugger
/// (`TAGCORE_LEAVE_HALTED_ON_MONITOR_TIMEOUT`).
pub(super) const LEAVE_HALTED_ON_MONITOR_TIMEOUT: bool = false;

// ARMv7-M / ARMv8-M debug and system control registers.
/// Debug Halting Control and Status Register: halt/step/resume the core and
/// read its state. Writes must carry [`DBGKEY`] in the top half.
pub const DHCSR: u32 = 0xE000_EDF0;
/// Debug Core Register Selector: writing a register number starts a
/// transfer between that core register and [`DCRDR`] (halted core only).
pub const DCRSR: u32 = 0xE000_EDF4;
/// Debug Core Register Data Register. Holds core-register values for
/// [`DCRSR`] transfers, and on STM32L4 tags doubles as the monitor's
/// command/result mailbox.
pub const DCRDR: u32 = 0xE000_EDF8;
/// Debug Exception and Monitor Control Register: vector catch and the
/// DebugMonitor exception controls ([`MON_EN`], [`MON_PEND`], [`MON_REQ`]).
pub const DEMCR: u32 = 0xE000_EDFC;
/// Interrupt Control and State Register: active and pending exception
/// numbers (VECTACTIVE bits 8..0, VECTPENDING bits 20..12).
pub const SCB_ICSR: u32 = 0xE000_ED04;
/// Vector Table Offset Register.
pub const SCB_VTOR: u32 = 0xE000_ED08;
/// Application Interrupt and Reset Control Register (priority grouping,
/// system reset request).
pub const SCB_AIRCR: u32 = 0xE000_ED0C;
/// System Control Register (SLEEPDEEP, SLEEPONEXIT).
pub const SCB_SCR: u32 = 0xE000_ED10;
/// System Handler Priority Register 1 (MemManage, BusFault, UsageFault).
pub const SCB_SHPR1: u32 = 0xE000_ED18;
/// System Handler Priority Register 2 (SVCall).
pub const SCB_SHPR2: u32 = 0xE000_ED1C;
/// System Handler Priority Register 3 (DebugMonitor, PendSV, SysTick);
/// DebugMonitor's priority decides whether a monitor request can run.
pub const SCB_SHPR3: u32 = 0xE000_ED20;
/// System Handler Control and State Register (handler active/pending/enable
/// bits, including MONITORACT).
pub const SCB_SHCSR: u32 = 0xE000_ED24;
/// Configurable Fault Status Register (MemManage, BusFault, UsageFault).
pub const SCB_CFSR: u32 = 0xE000_ED28;
/// HardFault Status Register.
pub const SCB_HFSR: u32 = 0xE000_ED2C;
/// Debug Fault Status Register (halt, breakpoint, vector-catch causes).
pub const SCB_DFSR: u32 = 0xE000_ED30;
/// MemManage Fault Address Register.
pub const SCB_MMFAR: u32 = 0xE000_ED34;
/// BusFault Address Register.
pub const SCB_BFAR: u32 = 0xE000_ED38;
/// Auxiliary Fault Status Register (implementation defined).
pub const SCB_AFSR: u32 = 0xE000_ED3C;
/// NVIC Interrupt Active Bit Register for IRQs 0-31, logged in U3 timeout
/// reports.
pub const NVIC_IABR0: u32 = 0xE000_E304;

/// Key that must be in DHCSR bits 31..16 for a write to take effect.
pub const DBGKEY: u32 = 0xA05F << 16;
/// DHCSR: enable halting debug. Set on every DHCSR write here; writing
/// [`DBGKEY`]` | C_DEBUGEN` alone resumes a halted core.
pub const C_DEBUGEN: u32 = 1 << 0;
/// DHCSR: halt the core.
pub const C_HALT: u32 = 1 << 1;
/// DHCSR: mask PendSV, SysTick and external interrupts while stepping or
/// running under debug; see `TagMonitor::clear_stale_mask_ints`, which
/// clears a stale one at attach.
pub const C_MASKINTS: u32 = 1 << 3;
/// DHCSR (read): a [`DCRSR`] transfer has completed.
pub const S_REGRDY: u32 = 1 << 16;
/// DHCSR (read): the core is halted.
pub const S_HALT: u32 = 1 << 17;
/// DHCSR (read): the core is locked up after an unrecoverable fault.
pub const S_LOCKUP: u32 = 1 << 19;
/// DHCSR (read, sticky, cleared on read): the core has been reset since
/// DHCSR was last read.
pub const S_RESET_ST: u32 = 1 << 25;
/// DEMCR vector catch on core reset; also the "host attached" flag the L4
/// firmware reads.
pub const VC_CORERESET: u32 = 1;
/// DEMCR: software-defined monitor request flag. The L4 host sets it with a
/// request and the firmware's DebugMon handler clears it when the result is
/// in [`DCRDR`]; it is the L4 "call complete" signal.
pub const MON_REQ: u32 = 1 << 19;
/// DEMCR: pend the DebugMonitor exception.
pub const MON_PEND: u32 = 1 << 17;
/// DEMCR: enable the DebugMonitor exception.
pub const MON_EN: u32 = 1 << 16;

/// Core registers read from a core halted for a timeout report.
///
/// Every register field is 0 when it could not be read or the core did not
/// halt; check `halted_dhcsr & S_HALT` before trusting them.
#[derive(Default)]
pub(super) struct HaltProbe {
    /// The probe, not something else, halted the core.
    pub(super) halted_by_probe: bool,
    /// DHCSR after the halt request; S_HALT set if the core stopped.
    pub(super) halted_dhcsr: u32,
    /// Current stack pointer (core register 13).
    pub(super) sp: u32,
    /// Link register (core register 14).
    pub(super) lr: u32,
    /// Program counter (core register 15, the debug return address).
    pub(super) pc: u32,
    /// Combined program status register (DCRSR selector 16).
    pub(super) xpsr: u32,
    /// Main stack pointer (selector 17).
    pub(super) msp: u32,
    /// Process stack pointer (selector 18).
    pub(super) psp: u32,
    /// CONTROL, FAULTMASK, BASEPRI and PRIMASK packed in one word
    /// (selector 20).
    pub(super) special: u32,
}

impl TagMonitor {
    /// Reads a core register of a halted core through DCRSR/DCRDR.
    ///
    /// `reg` is the DCRSR REGSEL value (0-12 general registers, 13 SP, 14
    /// LR, 15 PC, 16 xPSR, 17 MSP, 18 PSP, 20 special). Waits up to 100
    /// polls for S_REGRDY before and after the selection; `None` if the
    /// selection write fails or the transfer never completes.
    ///
    /// Overwrites DCRDR, which on an L4 is the monitor mailbox; only used
    /// after a call has already timed out.
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
    ///
    /// `dhcsr_snapshot` is DHCSR as read before the halt; it decides whether
    /// the core is locked up and whether the probe, rather than the
    /// firmware or an earlier debugger, is responsible for the halt
    /// (`halted_by_probe`), which tells the caller whether to resume it.
    /// **Leaves the core halted**; resuming is the caller's job.
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
    ///
    /// The words are R0-R3, R12, LR, the return address and xPSR, as the
    /// hardware stacks them on exception entry. `None` if the memory read
    /// fails (for example, a corrupt stack pointer).
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
