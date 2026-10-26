//! The STM32U3 transport: the shared block at [`MONITOR_SHARED_ADDR`].
//!
//! The host writes a command into the block and pends a reserved IRQ through
//! the NVIC; the firmware publishes `status` and `result` in the same block.
//! The session is requested by writing the block at the reset vector, and
//! kept alive by `host_activity`.

use std::thread::sleep;

use log::{debug, error, warn};

use super::abi::{
    DEBUGVERSION, MONITOR_CONNECTED_MAGIC, MONITOR_REQUEST_MAGIC, MONITOR_SHARED_ABI_VERSION,
    MONITOR_SHARED_ADDR, MONITOR_SHARED_FLAG_SESSION_READY, MONITOR_SHARED_KICK_ICPR_ADDR,
    MONITOR_SHARED_KICK_ISPR_ADDR, MONITOR_SHARED_KICK_MASK, MONITOR_SHARED_SIZE, MonitorShared,
    command_word, op, operation_name, status,
};
use super::cortex_m::*;
use super::{TagMonitor, ms};

impl TagMonitor {
    pub(super) fn read_monitor_shared(&mut self) -> Option<MonitorShared> {
        let mut bytes = [0u8; MonitorShared::SIZE];
        self.link
            .read_mem32(MONITOR_SHARED_ADDR, &mut bytes)
            .then(|| MonitorShared::from_bytes(&bytes))
    }

    pub(super) fn write_monitor_shared_word(&mut self, offset: u32, value: u32) -> bool {
        self.write_word(MONITOR_SHARED_ADDR + offset, value)
    }

    fn log_monitor_shared(&mut self, prefix: &str, shared: &MonitorShared) {
        error!(
            "{prefix} fields: request=0x{:x} abi={} debug=0x{:x} flags=0x{:x} buf=0x{:x} \
             size={} sha=0x{:x} command=0x{:x} result=0x{:x} status=0x{:x} heartbeat=0x{:x} \
             watchdog_ticks={} path=0x{:x}",
            shared.request,
            shared.abi_version,
            shared.debug_version,
            shared.flags,
            shared.buf_addr,
            shared.buf_size,
            shared.sha_addr,
            shared.command,
            shared.result,
            shared.status,
            shared.host_activity,
            shared.watchdog_ticks,
            shared.path_magic
        );
        let mut raw = [0u8; MONITOR_SHARED_SIZE as usize];
        if self.link.read_mem32(MONITOR_SHARED_ADDR, &mut raw) {
            let words: Vec<String> = raw
                .chunks(4)
                .map(|w| format!("{:08x}", u32::from_le_bytes(w.try_into().unwrap())))
                .collect();
            error!("{prefix} raw: {}", words.join(" "));
        } else {
            error!("{prefix} raw: shared block read failed");
        }
    }

    /// [`call`](Self::call) over the STM32U3 shared-block transport.
    pub(super) fn call_u3(&mut self, operation: u8, operand: i32) -> Option<u32> {
        const TIMEOUT_MS: u32 = 2500;
        const MONITORSTOP_TIMEOUT_MS: u32 = 100;
        let name = operation_name(operation);

        if !self.is_attached() {
            error!("monitor not attached");
            return None;
        }
        let Some(shared) = self.read_monitor_shared() else {
            error!("U3 monitor shared read failed");
            return None;
        };
        if operation != op::MONITORSTOP {
            if shared.request != MONITOR_CONNECTED_MAGIC {
                error!(
                    "U3 monitor not connected request=0x{:x} status=0x{:x}",
                    shared.request, shared.status
                );
                return None;
            }
            if shared.command != 0 || shared.status == status::PENDING {
                error!(
                    "U3 monitor busy command=0x{:x} status=0x{:x}",
                    shared.command, shared.status
                );
                return None;
            }
        }

        if !(self.write_monitor_shared_word(MonitorShared::RESULT, 0)
            && self.write_monitor_shared_word(MonitorShared::STATUS, status::IDLE)
            && self.write_monitor_shared_word(
                MonitorShared::COMMAND,
                command_word(operation, operand),
            )
            && self.write_word(MONITOR_SHARED_KICK_ICPR_ADDR, MONITOR_SHARED_KICK_MASK)
            && self.write_word(MONITOR_SHARED_KICK_ISPR_ADDR, MONITOR_SHARED_KICK_MASK))
        {
            error!("U3 monitor kick failed op={name}(0x{operation:x}) operand={operand}");
            return None;
        }

        let timeout_ms = if operation == op::MONITORSTOP {
            MONITORSTOP_TIMEOUT_MS
        } else {
            TIMEOUT_MS
        };
        for _ in 0..timeout_ms {
            if let Some(st) = self.read_word(MONITOR_SHARED_ADDR + MonitorShared::STATUS) {
                if st == status::DONE {
                    let result = self.read_word(MONITOR_SHARED_ADDR + MonitorShared::RESULT);
                    if result.is_none() {
                        error!("U3 monitor result read failed");
                    }
                    return result;
                }
                if operation == op::MONITORSTOP
                    && let Some(s) = self.read_monitor_shared()
                    && s.request == 0
                    && s.flags == 0
                    && s.command == 0
                {
                    debug!(
                        "U3 monitor stop observed detached state status=0x{:x} \
                                 result=0x{:x} heartbeat=0x{:x} watchdog_ticks={}",
                        s.status, s.result, s.host_activity, s.watchdog_ticks
                    );
                    return Some(1);
                }
                if matches!(
                    st,
                    status::BUSY | status::BAD_COMMAND | status::NOT_ATTACHED
                ) {
                    error!(
                        "U3 monitor request rejected op={name}(0x{operation:x}) operand={operand} \
                         status=0x{st:x}"
                    );
                    return None;
                }
            }
            sleep(ms(1));
        }

        if operation == op::MONITORSTOP {
            // Detach may race the terminal sleep path: the firmware clears the
            // shared block and its debug state, then sleeps. If standby wins
            // before the host sees the cleared block, SWD reads can fail even
            // though detach did what it needed to.
            warn!(
                "U3 monitor stop did not observe detached state before timeout; completing host \
                 detach"
            );
            return Some(1);
        }

        self.report_u3_timeout(operation, operand);
        None
    }

    /// Logs the target's state after a U3 call timed out, resuming the core
    /// if only the diagnostics halted it.
    fn report_u3_timeout(&mut self, operation: u8, operand: i32) {
        let shared = self.read_monitor_shared().unwrap_or_default();
        let iser = self
            .read_word(MONITOR_SHARED_KICK_ISPR_ADDR - 0x100)
            .unwrap_or(0);
        let ispr = self.read_word(MONITOR_SHARED_KICK_ISPR_ADDR).unwrap_or(0);
        let iabr = self.read_word(NVIC_IABR0).unwrap_or(0);
        let dhcsr = self.debug_or_zero(DHCSR);
        let demcr = self.debug_or_zero(DEMCR);
        let icsr = self.debug_or_zero(SCB_ICSR);
        let vtor = self.debug_or_zero(SCB_VTOR);
        let shcsr = self.debug_or_zero(SCB_SHCSR);
        let cfsr = self.debug_or_zero(SCB_CFSR);
        let hfsr = self.debug_or_zero(SCB_HFSR);
        let dfsr = self.debug_or_zero(SCB_DFSR);
        let probe = self.halt_for_diagnostics(dhcsr);

        error!(
            "U3 monitor call timed out op={}(0x{operation:x}) operand={operand} request=0x{:x} \
             command=0x{:x} status=0x{:x} result=0x{:x} flags=0x{:x} iser=0x{iser:x} \
             ispr=0x{ispr:x} iabr=0x{iabr:x} dhcsr=0x{dhcsr:x} demcr=0x{demcr:x} icsr=0x{icsr:x} \
             vtor=0x{vtor:x} shcsr=0x{shcsr:x} cfsr=0x{cfsr:x} hfsr=0x{hfsr:x} dfsr=0x{dfsr:x} \
             halt_dhcsr=0x{:x} sp=0x{:x} lr=0x{:x} pc=0x{:x} xpsr=0x{:x} msp=0x{:x} psp=0x{:x} \
             special=0x{:x}",
            operation_name(operation),
            shared.request,
            shared.command,
            shared.status,
            shared.result,
            shared.flags,
            probe.halted_dhcsr,
            probe.sp,
            probe.lr,
            probe.pc,
            probe.xpsr,
            probe.msp,
            probe.psp,
            probe.special,
        );

        if HALT_ON_MONITOR_TIMEOUT
            && !LEAVE_HALTED_ON_MONITOR_TIMEOUT
            && probe.halted_by_probe
            && probe.halted_dhcsr & S_HALT != 0
            && !self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN)
        {
            error!("U3 monitor timeout diagnostics halted target but failed to resume it");
        }
    }

    /// Starts a session on an STM32U3 target. On failure the session fields
    /// are cleared; the caller closes the probe.
    pub(super) fn attach_u3(&mut self) -> bool {
        if self.attach_u3_steps() {
            return true;
        }
        self.clear_session();
        false
    }

    fn attach_u3_steps(&mut self) -> bool {
        const ATTACH_TIMEOUT_MS: u32 = 2000;

        let Some(demcr) = self.link.read_debug32(DEMCR) else {
            error!("U3 monitor attach failed: DEMCR read failed");
            return false;
        };
        if !(self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN)
            && self.link.write_debug32(
                DEMCR,
                (demcr | VC_CORERESET) & !MON_EN & !MON_REQ & !MON_PEND,
            ))
        {
            error!("U3 monitor attach failed: debug setup failed");
            return false;
        }
        if !self.link.assert_reset(true) {
            error!("U3 monitor attach failed: reset release failed");
            return false;
        }

        let mut dhcsr = 0;
        for _ in 0..100 {
            if let Some(d) = self.link.read_debug32(DHCSR) {
                dhcsr = d;
                if d & S_HALT != 0 {
                    break;
                }
            }
            sleep(ms(1));
        }
        if dhcsr & S_HALT == 0 {
            error!(
                "U3 monitor attach failed: target did not halt at reset vector dhcsr=0x{dhcsr:x}"
            );
            return false;
        }

        // Halted at the reset vector: post the attach request before the
        // firmware's first instruction.
        let empty = [0u8; MONITOR_SHARED_SIZE as usize];
        if !(self.link.write_mem32(MONITOR_SHARED_ADDR, &empty)
            && self.write_monitor_shared_word(MonitorShared::REQUEST, MONITOR_REQUEST_MAGIC)
            && self.write_word(MONITOR_SHARED_KICK_ICPR_ADDR, MONITOR_SHARED_KICK_MASK))
        {
            error!("U3 monitor attach failed: shared request setup failed");
            return false;
        }
        match self.read_monitor_shared() {
            Some(s) => debug!(
                "U3 monitor request written at reset vector request=0x{:x} flags=0x{:x} \
                 status=0x{:x} dhcsr=0x{dhcsr:x}",
                s.request, s.flags, s.status
            ),
            None => error!("U3 monitor attach warning: reset-vector shared readback failed"),
        }

        self.clear_stale_mask_ints();
        if !self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN) {
            error!("U3 monitor attach failed: resume failed");
            return false;
        }

        let mut shared = MonitorShared::default();
        let mut last_nonzero = None;
        let mut connected = false;
        for _ in 0..ATTACH_TIMEOUT_MS {
            self.write_monitor_shared_word(MonitorShared::HOST_ACTIVITY, 1);
            if let Some(s) = self.read_monitor_shared() {
                shared = s;
                if s.any_nonzero() {
                    last_nonzero = Some(s);
                }
                if s.request == MONITOR_CONNECTED_MAGIC
                    && s.flags & MONITOR_SHARED_FLAG_SESSION_READY != 0
                {
                    connected = true;
                    break;
                }
            }
            sleep(ms(1));
        }

        if !connected {
            if let Some(s) = self.read_monitor_shared() {
                shared = s;
            }
            let ispr = self.read_word(MONITOR_SHARED_KICK_ISPR_ADDR).unwrap_or(0);
            let dhcsr = self.debug_or_zero(DHCSR);
            let demcr = self.debug_or_zero(DEMCR);
            error!(
                "U3 monitor attach timed out target={} idcode=0x{:x} request=0x{:x} flags=0x{:x} \
                 status=0x{:x} heartbeat=0x{:x} watchdog_ticks={} dhcsr=0x{dhcsr:x} \
                 demcr=0x{demcr:x} ispr=0x{ispr:x}",
                self.target_family,
                self.target_idcode,
                shared.request,
                shared.flags,
                shared.status,
                shared.host_activity,
                shared.watchdog_ticks
            );
            self.log_monitor_shared("U3 monitor attach timeout shared", &shared);
            if let Some(s) = last_nonzero {
                self.log_monitor_shared("U3 monitor attach last nonzero shared", &s);
            }
            return false;
        }

        if shared.abi_version != MONITOR_SHARED_ABI_VERSION {
            error!(
                "U3 monitor ABI mismatch host={MONITOR_SHARED_ABI_VERSION} target={}",
                shared.abi_version
            );
            return false;
        }

        self.version = shared.debug_version;
        self.call_buf = shared.buf_addr;
        self.maxpacket = shared.buf_size as usize;
        if self.version != DEBUGVERSION
            || self.call_buf == 0
            || self.maxpacket == 0
            || self.maxpacket > self.rpcbuf.len()
            || shared.sha_addr == 0
        {
            error!(
                "U3 monitor metadata invalid version=0x{:x} buf=0x{:x} size={} sha=0x{:x}",
                self.version, self.call_buf, self.maxpacket, shared.sha_addr
            );
            return false;
        }

        let mut sha = [0u8; 48];
        if !self.link.read_mem32(shared.sha_addr, &mut sha) {
            error!("U3 monitor attach failed: SHA read failed");
            return false;
        }
        self.sha_str = sha;
        debug!(
            "U3 monitor attached version=0x{:x} buf=0x{:x} size={} sha={}",
            self.version,
            self.call_buf,
            self.maxpacket,
            self.git_sha_string()
        );
        true
    }
}
