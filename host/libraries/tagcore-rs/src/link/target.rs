//! Raw access to the target through the probe: memory, debug registers,
//! nRST and supply voltage.
//!
//! Used by TagMonitor, and by tools that drive the debug port directly
//! (tag-monitor-test); in C++ these are protected members reached by
//! subclassing. Nothing here knows about the monitor: a write can halt,
//! reset or reconfigure the target.
//!
//! There are two access paths, and the monitor uses both:
//!
//! - **Memory** ([`LinkAdapt::read_mem32`], [`LinkAdapt::write_mem32`]): bulk
//!   32-bit transfers through the memory-access port, confirmed with a
//!   last-R/W-status query. Used for the RPC call buffer, the STM32U3 shared
//!   block and live memory access.
//! - **Debug registers** ([`LinkAdapt::read_debug32`],
//!   [`LinkAdapt::write_debug32`]): one word per command, with the status in
//!   the reply. Used for DHCSR, DEMCR, DCRDR and the other System Control
//!   Space registers, and for single peripheral words during attach.
//!
//! Every call blocks on USB (see [`super`]) and returns `false`/`None` with
//! the reason logged on failure; none retries. Callers that poll (the
//! monitor polls DEMCR or the shared block once per millisecond) rely on
//! the rate-limited logging in [`LinkAdapt::read_debug32`].

use std::time::Instant;

use log::{debug, error, warn};

use super::stlink::{self, debug_cmd, unpack16, unpack32};
use super::{INSTRUMENTED, LinkAdapt, elapsed_ns};

impl LinkAdapt {
    /// Target supply voltage in volts, as measured by the ST-LINK.
    ///
    /// The probe returns two ADC readings: its internal 1.2 V reference and
    /// the target-voltage pin through a divide-by-two, so the voltage is
    /// `2 * 1.2 * reading / reference`, as the C++ `LinkAdapt::Voltage`
    /// computes it. Sends nothing to the target. `None`, with "Voltage failed" logged, if the command
    /// fails; a reference reading of zero yields a non-finite value rather
    /// than `None`.
    pub fn voltage(&mut self) -> Option<f32> {
        let mut cmd = [0u8; 16];
        cmd[0] = stlink::GET_TARGET_VOLTAGE;
        let mut result = [0u8; 8];
        if self.cmd_read(&cmd, &mut result) {
            let factor = unpack32(&result[0..4]) as f32;
            let reading = unpack32(&result[4..8]) as f32;
            return Some(2.4 * reading / factor);
        }
        error!("Voltage failed");
        None
    }

    /// Reads `buf.len()` bytes of target memory with 32-bit accesses.
    ///
    /// `addr` and `buf.len()` must be multiples of 4 and the length at most
    /// 65535; the probe rejects anything else. The core is not halted.
    ///
    /// Returns false, with the reason logged, if the length does not fit
    /// the command's 16-bit count, the USB exchange fails, or the probe's
    /// last-R/W status is not OK (an access fault, for example to an
    /// unclocked peripheral or unmapped address). On failure `buf` may be
    /// partly or wholly overwritten with whatever the probe returned.
    /// Reading some peripheral registers has side effects on the target
    /// (clearing flags, popping FIFOs). Port of `LinkAdapt::ReadMem32`,
    /// which takes a `uint16_t` count; this checks the range instead.
    pub fn read_mem32(&mut self, addr: u32, buf: &mut [u8]) -> bool {
        let start = Instant::now();
        let Ok(count) = u16::try_from(buf.len()) else {
            error!("read of {} bytes exceeds one ST-LINK transfer", buf.len());
            return false;
        };
        let mut cmd = debug_cmd(stlink::DEBUG_READMEM_32BIT, addr);
        cmd[6..8].copy_from_slice(&count.to_le_bytes());
        if !self.cmd_read(&cmd, buf) {
            return false;
        }
        match self.get_rw_status() {
            Some(stlink::DEBUG_ERR_OK) => {}
            status => {
                error!("error return {:x}", status.unwrap_or(0));
                return false;
            }
        }
        if INSTRUMENTED {
            self.stats.read_mem_calls += 1;
            self.stats.read_mem_bytes += u64::from(count);
            self.stats.read_mem_ns += elapsed_ns(start);
        }
        true
    }

    /// Writes `data` to target memory with 32-bit accesses. Same constraints
    /// as [`read_mem32`](Self::read_mem32).
    ///
    /// Returns false, with the reason logged, on an oversize length, a USB
    /// failure or a non-OK last-R/W status; part of the data may already
    /// have been written. Nothing about the destination is checked: writing
    /// the wrong address can corrupt firmware state, reconfigure a
    /// peripheral or reset the part. Port of `LinkAdapt::WriteMem32`.
    pub fn write_mem32(&mut self, addr: u32, data: &[u8]) -> bool {
        let start = Instant::now();
        let Ok(count) = u16::try_from(data.len()) else {
            error!("write of {} bytes exceeds one ST-LINK transfer", data.len());
            return false;
        };
        let mut cmd = debug_cmd(stlink::DEBUG_WRITEMEM_32BIT, addr);
        cmd[6..8].copy_from_slice(&count.to_le_bytes());
        if !self.cmd_write(&cmd, data) {
            return false;
        }
        match self.get_rw_status() {
            Some(stlink::DEBUG_ERR_OK) => {}
            status => {
                error!("error return {:x}", status.unwrap_or(0));
                return false;
            }
        }
        if INSTRUMENTED {
            self.stats.write_mem_calls += 1;
            self.stats.write_mem_bytes += u64::from(count);
            self.stats.write_mem_ns += elapsed_ns(start);
        }
        true
    }

    /// Reads one 32-bit word through the debug-register path.
    ///
    /// Intended for the System Control Space (DHCSR, DEMCR, DCRDR, SCB) and
    /// single peripheral words such as DBGMCU_IDCODE. `None` if the USB
    /// exchange fails (logged by the transfer layer) or the probe's status
    /// is not OK (`0x80`).
    ///
    /// Status failures are rate-limited in the log because callers poll
    /// this in tight loops and a base can fail a burst of reads (status
    /// 0x81) and then recover: the first failure of a run is logged at
    /// ERROR, the rest at DEBUG, and the next success logs a WARN with the
    /// length of the run if it was longer than one. Only successful reads
    /// are counted in the statistics. Port of `LinkAdapt::ReadDebug32`.
    pub fn read_debug32(&mut self, addr: u32) -> Option<u32> {
        let start = Instant::now();
        let mut result = [0u8; 8];
        if !self.cmd_read(
            &debug_cmd(stlink::DEBUG_APIV2_READDEBUGREG, addr),
            &mut result,
        ) {
            return None;
        }
        let status = unpack16(&result);
        if status != stlink::DEBUG_ERR_OK {
            // Callers poll; a base can fail a burst of reads (status 0x81)
            // and then recover, so only the first of a run is an error.
            self.debug_read_failures += 1;
            if self.debug_read_failures == 1 {
                error!("read debug register 0x{addr:x} failed status=0x{status:x}");
            } else {
                debug!("read debug register 0x{addr:x} failed status=0x{status:x}");
            }
            return None;
        }
        if self.debug_read_failures > 1 {
            warn!(
                "{} debug register reads failed in a row before 0x{addr:x} read",
                self.debug_read_failures
            );
        }
        self.debug_read_failures = 0;
        if INSTRUMENTED {
            self.stats.read_debug_calls += 1;
            self.stats.read_debug_ns += elapsed_ns(start);
        }
        Some(unpack32(&result[4..8]))
    }

    /// Writes one 32-bit word through the debug-register path.
    ///
    /// Returns false, with the reason logged, if the USB exchange fails or
    /// the probe's status is not OK. Writes to DHCSR, DEMCR or the SCB can
    /// halt, resume or reset the core; this function does not care. Port of
    /// `LinkAdapt::WriteDebug32`.
    pub fn write_debug32(&mut self, addr: u32, val: u32) -> bool {
        let start = Instant::now();
        let mut cmd = debug_cmd(stlink::DEBUG_APIV2_WRITEDEBUGREG, addr);
        cmd[6..10].copy_from_slice(&val.to_le_bytes());
        let mut result = [0u8; 2];
        if !self.cmd_read(&cmd, &mut result) {
            return false;
        }
        let status = unpack16(&result);
        if status != stlink::DEBUG_ERR_OK {
            error!("write debug register 0x{addr:x} failed status=0x{status:x}");
            return false;
        }
        if INSTRUMENTED {
            self.stats.write_debug_calls += 1;
            self.stats.write_debug_ns += elapsed_ns(start);
        }
        true
    }

    /// Drives the target's nRST line: `high` releases reset, low holds it.
    ///
    /// Holding reset ends whatever the tag was doing; releasing it boots the
    /// firmware (or, with vector catch armed in DEMCR, halts it at the reset
    /// vector). The monitor's attach sequences depend on that ordering.
    /// Returns false, with the status logged, if the command fails. Port of
    /// `LinkAdapt::AssertReset`.
    pub fn assert_reset(&mut self, high: bool) -> bool {
        let mut cmd = debug_cmd(stlink::JTAG_DRIVE_NRST, 0);
        cmd[2] = u8::from(high);
        let mut result = [0u8; 2];
        if !self.cmd_read(&cmd, &mut result) {
            return false;
        }
        let status = unpack16(&result);
        if status != stlink::DEBUG_ERR_OK {
            error!("assert reset failed: {status:x}");
            return false;
        }
        true
    }
}
