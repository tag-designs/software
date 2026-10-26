//! The STM32L4 transport: the DebugMonitor exception and the DCRDR mailbox.
//!
//! The host writes a command word into DCRDR and pends DebugMon through
//! DEMCR; the handler runs it and clears MON_REQ when its result is back in
//! DCRDR. VC_CORERESET doubles as the "host attached" flag the firmware
//! reads, and halts the core at the reset vector after a reset.

use std::thread::sleep;

use log::{debug, error, warn};

use super::abi::{command_word, info, op, operation_name};
use super::cortex_m::*;
use super::{STM32L4_DBGMCU_IDCODE, TagMonitor, TargetFamily, ms};

// The flash empty check is an STM32L4 feature. On the STM32U3 FLASH_SR is at
// offset 0x20, so this address is FLASH_OPTKEYR there; gate on the device ID.
const L4_DBGMCU_DEV_ID_MASK: u32 = 0x0000_0FFF;
const L4_DEV_ID_L43X: u32 = 0x435;
const L4_FLASH_SR: u32 = 0x4002_2010;
const L4_FLASH_SR_PEMPTY: u32 = 1 << 17;

impl TagMonitor {
    /// [`call`](Self::call) over the STM32L4 DebugMonitor/DCRDR transport.
    ///
    /// [`op::MONITORSTOP`] returns 1 without waiting for the firmware to
    /// answer, because the tag may drop into low power as soon as it has seen
    /// the request. Returns `None`, with the reason logged, if not attached,
    /// the monitor is busy, the link fails or the firmware does not answer in
    /// time (500 ms for result reads; 2.5 s per call, or 65 s for a protobuf
    /// call on L4).
    pub(super) fn call_l4(&mut self, operation: u8, operand: i32) -> Option<u32> {
        const MONITOR_RESULT_TIMEOUT_MS: u32 = 500;
        const MONITORSTOP_POST_MS: u64 = 5;
        const MONITOR_FAST_CALL_TIMEOUT_MS: u32 = 2500;
        const MONITOR_PROTOBUF_CALL_TIMEOUT_MS: u32 = 65000;
        let call_timeout_ms = if operation == op::PROTOBUF {
            MONITOR_PROTOBUF_CALL_TIMEOUT_MS
        } else {
            MONITOR_FAST_CALL_TIMEOUT_MS
        };
        let name = operation_name(operation);

        if !self.is_attached() {
            error!("monitor not attached");
            return None;
        }

        // VC_CORERESET is the attachment flag the embedded app reads; it also
        // halts the core at the reset vector after a reset.
        let Some(mut demcr) = self.link.read_debug32(DEMCR) else {
            error!("read reg failed");
            return None;
        };

        // DCRDR is both the request and the response mailbox: do not write a
        // new request until the previous DebugMon transaction is idle.
        if demcr & (MON_PEND | MON_REQ) != 0 {
            error!(
                "monitor pending bit already set op={name}(0x{operation:x}) operand={operand} \
                 demcr=0x{demcr:x}"
            );
            return None;
        }

        if !self
            .link
            .write_debug32(DCRDR, command_word(operation, operand))
            || !self
                .link
                .write_debug32(DEMCR, demcr | MON_EN | MON_PEND | MON_REQ | VC_CORERESET)
        {
            error!("write reg failed");
            return None;
        }

        if operation == op::MONITORSTOP {
            // Detach lets the target drop into terminal low power as soon as
            // the handler has seen the request; waiting for a reply could turn
            // a successful detach into a USB/SWD timeout.
            sleep(ms(MONITORSTOP_POST_MS));
            return Some(1);
        }

        // Wait for the handler to clear MON_REQ.
        let mut answered = false;
        for _ in 0..call_timeout_ms {
            match self.link.read_debug32(DEMCR) {
                None => error!("read_mem failed"),
                Some(d) => {
                    demcr = d;
                    if d & MON_REQ == 0 {
                        answered = true;
                        break;
                    }
                }
            }
            sleep(ms(1));
        }

        if !answered {
            self.report_l4_timeout(operation, operand, demcr);
            return None;
        }

        for _ in 0..MONITOR_RESULT_TIMEOUT_MS {
            if let Some(result) = self.link.read_debug32(DCRDR) {
                return Some(result);
            }
            error!("read_mem failed");
            sleep(ms(1));
        }
        error!(
            "monitor result read timed out op={name}(0x{operation:x}) operand={operand} \
             demcr=0x{demcr:x}"
        );
        None
    }

    /// Logs the target's state after an L4 call timed out, and recovers what
    /// it can: a core caught at the reset vector is released and the session
    /// dropped; a core halted only by the diagnostics is resumed.
    fn report_l4_timeout(&mut self, operation: u8, operand: i32, demcr: u32) {
        let dhcsr = self.debug_or_zero(DHCSR);
        let dcrdr = self.debug_or_zero(DCRDR);
        let (rcc_csr_addr, rcc_csr, rcc_csr_ok) = self.read_target_rcc_csr();
        let icsr = self.debug_or_zero(SCB_ICSR);
        let aircr = self.debug_or_zero(SCB_AIRCR);
        let scr = self.debug_or_zero(SCB_SCR);
        let shpr1 = self.debug_or_zero(SCB_SHPR1);
        let shpr2 = self.debug_or_zero(SCB_SHPR2);
        let shpr3 = self.debug_or_zero(SCB_SHPR3);
        let shcsr = self.debug_or_zero(SCB_SHCSR);
        let vectactive = icsr & 0x1ff;
        let rettobase = (icsr >> 11) & 1;
        let vectpending = (icsr >> 12) & 0x1ff;
        let isrpending = (icsr >> 22) & 1;
        let cfsr = self.debug_or_zero(SCB_CFSR);
        let hfsr = self.debug_or_zero(SCB_HFSR);
        let dfsr = self.debug_or_zero(SCB_DFSR);
        let mmfar = self.debug_or_zero(SCB_MMFAR);
        let bfar = self.debug_or_zero(SCB_BFAR);
        let afsr = self.debug_or_zero(SCB_AFSR);

        let probe = self.halt_for_diagnostics(dhcsr);
        let (msp_frame, psp_frame) = if probe.halted_dhcsr & S_HALT != 0 {
            (self.read_frame(probe.msp), self.read_frame(probe.psp))
        } else {
            (None, None)
        };
        let frame = |f: Option<[u32; 8]>| {
            let w = f.unwrap_or_default();
            format!(
                "ok={} lr=0x{:x} pc=0x{:x} xpsr=0x{:x}",
                u8::from(f.is_some()),
                w[5],
                w[6],
                w[7]
            )
        };

        error!(
            "monitor call timed out op={}(0x{operation:x}) operand={operand} target={} \
             idcode=0x{:x} demcr=0x{demcr:x} dhcsr=0x{dhcsr:x} dcrdr=0x{dcrdr:x} \
             rcc_csr_addr=0x{rcc_csr_addr:x} rcc_csr=0x{rcc_csr:x} rcc_csr_ok={} \
             icsr=0x{icsr:x} active={vectactive} pending={vectpending} isrpending={isrpending} \
             rettobase={rettobase} shcsr=0x{shcsr:x} shpr1=0x{shpr1:x} shpr2=0x{shpr2:x} \
             shpr3=0x{shpr3:x} aircr=0x{aircr:x} scr=0x{scr:x} halt_dhcsr=0x{:x} sp=0x{:x} \
             lr=0x{:x} pc=0x{:x} xpsr=0x{:x} msp=0x{:x} psp=0x{:x} special=0x{:x} \
             msp_frame {} psp_frame {} cfsr=0x{cfsr:x} hfsr=0x{hfsr:x} dfsr=0x{dfsr:x} \
             mmfar=0x{mmfar:x} bfar=0x{bfar:x} afsr=0x{afsr:x}",
            operation_name(operation),
            self.target_family,
            self.target_idcode,
            u8::from(rcc_csr_ok),
            probe.halted_dhcsr,
            probe.sp,
            probe.lr,
            probe.pc,
            probe.xpsr,
            probe.msp,
            probe.psp,
            probe.special,
            frame(msp_frame),
            frame(psp_frame),
        );

        let reset_vector_catch = dhcsr & (S_HALT | S_RESET_ST) == (S_HALT | S_RESET_ST)
            && vectactive == 0
            && cfsr == 0
            && hfsr == 0;

        if reset_vector_catch {
            // The target reset during the session: the firmware's monitor
            // state is gone, and further PROTOBUF calls would only re-pend
            // DebugMon at Reset_Handler. Clear vector catch, resume, and drop
            // the host session; the next operation must attach again.
            let resume_demcr = (demcr | MON_EN) & !MON_REQ & !MON_PEND & !VC_CORERESET;
            if self.link.write_debug32(DEMCR, resume_demcr)
                && self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN)
            {
                let mut resume_dhcsr = 0;
                for _ in 0..20 {
                    if let Some(d) = self.link.read_debug32(DHCSR) {
                        resume_dhcsr = d;
                        if d & S_HALT == 0 {
                            break;
                        }
                    }
                    sleep(ms(1));
                }
                let outcome = if resume_dhcsr & S_HALT == 0 {
                    "cleared vector catch and released reset handler"
                } else {
                    "target still halted after release"
                };
                error!(
                    "monitor caught target reset; {outcome} pc=0x{:x} target={} \
                     rcc_csr_addr=0x{rcc_csr_addr:x} rcc_csr=0x{rcc_csr:x} dhcsr=0x{resume_dhcsr:x}",
                    probe.pc, self.target_family
                );
            } else {
                error!("monitor caught target reset but failed to resume target");
            }
            // A reset into system memory is the ROM bootloader. On the L4 that
            // happens with BOOT0 low when FLASH_SR.PEMPTY is set; it is latched
            // at power-on or option-byte load, so it survives reflashing.
            if self.target_family == TargetFamily::Stm32L4 && probe.pc & 0xFFFF_0000 == 0x1FFF_0000
            {
                let flash_sr = self.link.read_debug32(L4_FLASH_SR);
                error!(
                    "target is running the ROM bootloader (pc=0x{:x}), not the firmware; \
                     FLASH_SR={}{}. If PEMPTY (bit 17) is set, power-cycle the tag or reload \
                     its option bytes",
                    probe.pc,
                    flash_sr.map_or("unreadable".into(), |v| format!("0x{v:x}")),
                    if flash_sr.is_some_and(|v| v & L4_FLASH_SR_PEMPTY != 0) {
                        " (PEMPTY set)"
                    } else {
                        ""
                    }
                );
            }
            self.clear_all();
            self.link.detach();
        } else if HALT_ON_MONITOR_TIMEOUT
            && !LEAVE_HALTED_ON_MONITOR_TIMEOUT
            && probe.halted_by_probe
            && probe.halted_dhcsr & S_HALT != 0
        {
            let resume_demcr = (demcr | MON_EN | VC_CORERESET) & !MON_REQ & !MON_PEND;
            if self.link.write_debug32(DEMCR, resume_demcr)
                && self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN)
            {
                error!(
                    "monitor timeout diagnostics halted target; cleared pending request and resumed core"
                );
            } else {
                error!("monitor timeout diagnostics halted target but failed to resume it");
            }
        }
    }

    /// Arms vector catch and the monitor, then releases reset so the L4
    /// firmware boots seeing the host attached.
    fn reset_into_monitor(&mut self, phase: &str) -> bool {
        let Some(setup_demcr) = self.link.read_debug32(DEMCR) else {
            error!("Monitor attach failed: {phase} DEMCR read failed");
            return false;
        };
        // Standby can power-cycle the core debug block, so program vector
        // catch immediately before releasing reset; the firmware then sees
        // the host before it can return to terminal standby.
        if !(self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN)
            && self.link.write_debug32(
                DEMCR,
                (setup_demcr | MON_EN | VC_CORERESET) & !MON_REQ & !MON_PEND,
            ))
        {
            error!("Monitor attach failed: {phase} debug setup failed");
            return false;
        }
        if !self.link.assert_reset(true) {
            error!("Monitor attach failed: {phase} reset release failed");
            return false;
        }
        sleep(ms(50));
        self.clear_stale_mask_ints();
        if !self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN) {
            warn!("Monitor attach warning: {phase} halt release failed");
        }
        true
    }

    /// Starts a session on an STM32L4 (or unidentified) target. On failure
    /// the probe is closed.
    pub(super) fn attach_l4(&mut self) -> bool {
        if self.attach_l4_steps() {
            return true;
        }
        self.link.detach();
        false
    }

    fn attach_l4_steps(&mut self) -> bool {
        if !self.reset_into_monitor("initial") {
            return false;
        }

        // With nSWBOOT0 = 1 and BOOT0 low, a set FLASH_SR.PEMPTY sends every
        // reset to the ROM bootloader, so no monitor will answer. Hardware
        // evaluates it only at power-on or option-byte load (RM0394 2.6);
        // `flash_release.py` clears it after programming, so this reports a
        // tag erased by some other route. Gated on the device ID because the
        // address is FLASH_SR only on an L4.
        if self
            .link
            .read_debug32(STM32L4_DBGMCU_IDCODE)
            .is_some_and(|id| id & L4_DBGMCU_DEV_ID_MASK == L4_DEV_ID_L43X)
            && let Some(flash_sr) = self.link.read_debug32(L4_FLASH_SR)
            && flash_sr & L4_FLASH_SR_PEMPTY != 0
        {
            error!(
                "FLASH_SR.PEMPTY is set (FLASH_SR=0x{flash_sr:x}): the tag boots the ROM \
                         bootloader, not the firmware. Power-cycle the tag, or reload its option \
                         bytes"
            );
        }

        self.version = match self.call(op::TAG_MONITORINFO, info::MONITORVERSION) {
            Some(version) => version,
            None => {
                warn!("L4 monitor version read failed; retrying attach after reset pulse");
                if !self.link.assert_reset(false) {
                    error!("Monitor attach failed: retry reset assert failed");
                    return false;
                }
                sleep(ms(20));
                let retried = if self.reset_into_monitor("retry") {
                    self.call(op::TAG_MONITORINFO, info::MONITORVERSION)
                } else {
                    None
                };
                let Some(version) = retried else {
                    error!("couldn't fetch monitor version information");
                    return false;
                };
                version
            }
        };
        debug!("Monitor Version 0x{:x}", self.version);

        let Some(call_buf) = self.call(op::TAG_MONITORINFO, info::MONITORBUF) else {
            error!("couldn't fetch monitor buffer location");
            return false;
        };
        self.call_buf = call_buf;

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
        debug!("Tag Hash String 0x{sha_addr:x}, {}", self.git_sha_string());

        let Some(size) = self.call(op::TAG_MONITORINFO, info::MONITORBUFSIZE) else {
            error!("couldn't fetch monitor buffer length");
            return false;
        };
        self.maxpacket = size as usize;
        if self.maxpacket > self.rpcbuf.len() {
            error!("RPC buffer is too small");
            return false;
        }

        if !matches!(self.call(op::MONITORSTART, 0), Some(s) if s != 0) {
            error!("Monitor Start failed");
            return false;
        }
        true
    }
}
