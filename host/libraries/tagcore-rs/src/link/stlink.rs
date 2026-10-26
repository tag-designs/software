//! The ST-LINK USB protocol: command and status codes, the 16-byte command
//! block, bulk transfers, and the probe's mode changes (DFU, SWD).

use std::time::{Duration, Instant};

use log::{error, warn};

use super::{INSTRUMENTED, LinkAdapt, elapsed_ns, libusb_name};

// ST-LINK USB command and status codes.
pub const GET_CURRENT_MODE: u8 = 0xF5;
pub const GET_TARGET_VOLTAGE: u8 = 0xF7;

pub const DEV_DFU_MODE: u8 = 0x00;
pub const DEV_DEBUG_MODE: u8 = 0x02;

pub const JTAG_DRIVE_NRST: u8 = 0x3C;
pub const DEBUG_COMMAND: u8 = 0xF2;
pub const DFU_COMMAND: u8 = 0xF3;
pub const DFU_EXIT: u8 = 0x07;
pub const DEBUG_ERR_OK: u16 = 0x80;

// Debug sub-commands (commands.h, from the stlink-org project).
pub const DEBUG_READMEM_32BIT: u8 = 0x07;
pub const DEBUG_WRITEMEM_32BIT: u8 = 0x08;
pub const DEBUG_EXIT: u8 = 0x21;
pub const DEBUG_APIV2_ENTER: u8 = 0x30;
pub const DEBUG_APIV2_WRITEDEBUGREG: u8 = 0x35;
pub const DEBUG_APIV2_READDEBUGREG: u8 = 0x36;
pub const DEBUG_APIV2_GETLASTRWSTATUS: u8 = 0x3B;
pub const DEBUG_APIV2_GETLASTRWSTATUS2: u8 = 0x3E;
pub const DEBUG_ENTER_SWD: u8 = 0xA3;

pub const USB_VID_ST: u16 = 0x0483;
pub const USB_PID_STLINK_32L: u16 = 0x3748;
pub const USB_PID_STLINK_32L_AUDIO: u16 = 0x374a;
pub const USB_PID_STLINK_NUCLEO: u16 = 0x374b;
pub const USB_PID_STLINK_V2_1: u16 = 0x3752;
pub const USB_PID_STLINK_V3E: u16 = 0x374e;
pub const USB_PID_STLINK_V3S: u16 = 0x374f;
pub const USB_PID_STLINK_V3_2VCP: u16 = 0x3753;
pub const USB_PID_STLINK_V3_PWR: u16 = 0x3757;

/// Timeout for one USB bulk transfer.
const USB_TRANSFER_TIMEOUT: Duration = Duration::from_millis(1500);

pub(super) fn unpack16(buf: &[u8]) -> u16 {
    u16::from_le_bytes([buf[0], buf[1]])
}

pub(super) fn unpack32(buf: &[u8]) -> u32 {
    u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]])
}

/// The ST-LINK mode code: the low byte of a GET_CURRENT_MODE reply.
pub(super) fn mode_code(mode: u16) -> u8 {
    (mode & 0xff) as u8
}

/// A debug command block: `F2 <sub> <addr:le32>`, the rest zero.
pub(super) fn debug_cmd(sub: u8, addr: u32) -> [u8; 16] {
    let mut cmd = [0u8; 16];
    cmd[0] = DEBUG_COMMAND;
    cmd[1] = sub;
    cmd[2..6].copy_from_slice(&addr.to_le_bytes());
    cmd
}

impl LinkAdapt {
    fn usb_in(&mut self, buf: &mut [u8]) -> bool {
        let Some(handle) = &self.handle else {
            error!("USB transfer with no device open");
            return false;
        };
        let start = Instant::now();
        let res = match handle.read_bulk(self.ep_in, buf, USB_TRANSFER_TIMEOUT) {
            Ok(n) => n,
            Err(e) => {
                error!("{}", libusb_name(&e));
                return false;
            }
        };
        if INSTRUMENTED {
            self.stats.usb_in_calls += 1;
            self.stats.usb_in_bytes += res as u64;
            self.stats.usb_in_ns += elapsed_ns(start);
        }
        if res != buf.len() {
            error!("Transferred {res} bytes instead of {}", buf.len());
            return false;
        }
        true
    }

    fn usb_out(&mut self, buf: &[u8]) -> bool {
        let Some(handle) = &self.handle else {
            error!("USB transfer with no device open");
            return false;
        };
        let start = Instant::now();
        let res = match handle.write_bulk(self.ep_out, buf, USB_TRANSFER_TIMEOUT) {
            Ok(n) => n,
            Err(e) => {
                error!("{}", libusb_name(&e));
                return false;
            }
        };
        if INSTRUMENTED {
            self.stats.usb_out_calls += 1;
            self.stats.usb_out_bytes += res as u64;
            self.stats.usb_out_ns += elapsed_ns(start);
        }
        if res != buf.len() {
            error!("Transferred {res} bytes instead of {}", buf.len());
            return false;
        }
        true
    }

    /// Sends a command block, then reads `buf.len()` bytes if non-empty.
    pub(super) fn cmd_read(&mut self, cmd: &[u8; 16], buf: &mut [u8]) -> bool {
        let start = Instant::now();
        if !self.usb_out(cmd) {
            error!("cmd_eval request failed");
            return false;
        }
        if !buf.is_empty() && !self.usb_in(buf) {
            error!("cmd_eval response failed");
            return false;
        }
        self.count_cmd(start);
        true
    }

    /// Sends a command block, then writes `buf` if non-empty.
    pub(super) fn cmd_write(&mut self, cmd: &[u8; 16], buf: &[u8]) -> bool {
        let start = Instant::now();
        if !self.usb_out(cmd) {
            error!("cmd_eval request failed");
            return false;
        }
        if !buf.is_empty() && !self.usb_out(buf) {
            error!("cmd_eval response failed");
            return false;
        }
        self.count_cmd(start);
        true
    }

    fn count_cmd(&mut self, start: Instant) {
        if INSTRUMENTED {
            self.stats.cmd_calls += 1;
            self.stats.cmd_ns += elapsed_ns(start);
        }
    }

    pub(super) fn current_mode(&mut self) -> Option<u16> {
        let mut cmd = [0u8; 16];
        cmd[0] = GET_CURRENT_MODE;
        let mut result = [0u8; 2];
        self.cmd_read(&cmd, &mut result).then(|| unpack16(&result))
    }

    pub(super) fn exit_debug_mode(&mut self) -> bool {
        self.cmd_read(&debug_cmd(DEBUG_EXIT, 0), &mut [])
    }

    pub(super) fn enter_debug_mode(&mut self) -> bool {
        let mut cmd = debug_cmd(DEBUG_APIV2_ENTER, 0);
        cmd[2] = DEBUG_ENTER_SWD;
        let mut result = [0u8; 2];
        if !self.cmd_read(&cmd, &mut result) {
            return false;
        }
        let status = unpack16(&result);
        if status != DEBUG_ERR_OK {
            warn!("enter swd mode reported status 0x{status:x}");
        }
        true
    }

    pub(super) fn exit_dfu_mode(&mut self) -> bool {
        let mut cmd = [0u8; 16];
        cmd[0] = DFU_COMMAND;
        cmd[1] = DFU_EXIT;
        self.cmd_read(&cmd, &mut [])
    }

    pub(super) fn get_rw_status(&mut self) -> Option<u16> {
        let start = Instant::now();
        let mut result = [0u8; 12];
        let status = if self.cmd_read(&debug_cmd(DEBUG_APIV2_GETLASTRWSTATUS2, 0), &mut result) {
            unpack16(&result)
        } else {
            // Older probe firmware has only the two-byte form.
            let mut legacy = [0u8; 2];
            if !self.cmd_read(&debug_cmd(DEBUG_APIV2_GETLASTRWSTATUS, 0), &mut legacy) {
                return None;
            }
            unpack16(&legacy)
        };
        if INSTRUMENTED {
            self.stats.rw_status_calls += 1;
            self.stats.rw_status_ns += elapsed_ns(start);
        }
        Some(status)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn debug_command_blocks_are_little_endian() {
        let cmd = debug_cmd(DEBUG_READMEM_32BIT, 0x2000_0010);
        assert_eq!(&cmd[..6], &[0xF2, 0x07, 0x10, 0x00, 0x00, 0x20]);
        assert!(cmd[6..].iter().all(|&b| b == 0));
    }
}
