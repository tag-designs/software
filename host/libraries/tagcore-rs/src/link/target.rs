//! Raw access to the target through the probe: memory, debug registers,
//! nRST and supply voltage.
//!
//! Used by TagMonitor, and by tools that drive the debug port directly
//! (tag-monitor-test); in C++ these are protected members reached by
//! subclassing. Nothing here knows about the monitor: a write can halt,
//! reset or reconfigure the target.

use std::time::Instant;

use log::{debug, error, warn};

use super::stlink::{self, debug_cmd, unpack16, unpack32};
use super::{INSTRUMENTED, LinkAdapt, elapsed_ns};

impl LinkAdapt {
    /// Target supply voltage in volts, as measured by the ST-LINK.
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
