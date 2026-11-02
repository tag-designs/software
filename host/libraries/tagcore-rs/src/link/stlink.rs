//! The ST-LINK USB protocol: command and status codes, the 16-byte command
//! block, bulk transfers, and the probe's mode changes (DFU, SWD).
//!
//! Port of the private half of `linkadapt.cc` (`usb_xfer`, `cmd_eval`,
//! `current_mode`, `enter_debug_mode` and friends) and of the command codes
//! in `commands.h`. Every exchange is a 16-byte command block on the bulk
//! OUT endpoint, then an optional data phase: [`LinkAdapt::cmd_read`] reads
//! a reply on the IN endpoint, [`LinkAdapt::cmd_write`] sends a payload on
//! the OUT endpoint. Multi-byte fields are little-endian. Each bulk transfer
//! blocks for at most [`USB_TRANSFER_TIMEOUT`].
//!
//! Nothing here is aware of the target; the target-facing commands built on
//! these blocks live in `target.rs`, and the base-identify command is the
//! one exchange that talks to the base's own firmware rather than the tag.

use std::time::{Duration, Instant};

use log::{debug, error, log, warn};

use super::{INSTRUMENTED, LinkAdapt, elapsed_ns, libusb_name};

// ST-LINK USB command and status codes.
/// Top-level command: report the probe's current mode (2-byte reply; the
/// low byte is the mode code, see [`mode_code`]).
pub const GET_CURRENT_MODE: u8 = 0xF5;
/// Top-level command: report the target-voltage ADC readings (8-byte reply:
/// reference reading, then target reading, each a little-endian `u32`).
pub const GET_TARGET_VOLTAGE: u8 = 0xF7;

/// Mode code: the probe is in its DFU (firmware-update) mode and must leave
/// it before any debug command.
pub const DEV_DFU_MODE: u8 = 0x00;
/// Mode code: the probe is in debug (SWD/JTAG) mode.
pub const DEV_DEBUG_MODE: u8 = 0x02;

/// Debug sub-command: drive the target's nRST line (byte 2: 0 low, 1 high).
pub const JTAG_DRIVE_NRST: u8 = 0x3C;
/// Top-level command prefix for every debug sub-command (byte 0 of the
/// block, sub-command in byte 1).
pub const DEBUG_COMMAND: u8 = 0xF2;
/// Top-level command prefix for DFU sub-commands.
pub const DFU_COMMAND: u8 = 0xF3;
/// DFU sub-command: leave DFU mode and return to the probe's idle mode.
pub const DFU_EXIT: u8 = 0x07;
/// Status code meaning success, in the first two bytes of a debug reply.
/// Anything else is a failure (0x81 is the common "fault" a base returns
/// during a burst of failed debug-register reads).
pub const DEBUG_ERR_OK: u16 = 0x80;

// Debug sub-commands (commands.h, from the stlink-org project).
/// Read target memory with 32-bit accesses through the memory-access port
/// (address in bytes 2..6, byte count in bytes 6..8). Does not halt the core.
pub const DEBUG_READMEM_32BIT: u8 = 0x07;
/// Write target memory with 32-bit accesses; the data follows on the OUT
/// endpoint. Does not halt the core.
pub const DEBUG_WRITEMEM_32BIT: u8 = 0x08;
/// Leave debug mode.
pub const DEBUG_EXIT: u8 = 0x21;
/// Enter debug mode (API v2); byte 2 selects the transport,
/// [`DEBUG_ENTER_SWD`] here.
pub const DEBUG_APIV2_ENTER: u8 = 0x30;
/// Write one 32-bit word through the debug-register path (address in bytes
/// 2..6, value in bytes 6..10); 2-byte status reply.
pub const DEBUG_APIV2_WRITEDEBUGREG: u8 = 0x35;
/// Read one 32-bit word through the debug-register path; 8-byte reply,
/// status in bytes 0..2 and the value in bytes 4..8.
pub const DEBUG_APIV2_READDEBUGREG: u8 = 0x36;
/// Status of the last memory transfer, 2-byte form understood by older
/// probe firmware.
pub const DEBUG_APIV2_GETLASTRWSTATUS: u8 = 0x3B;
/// Status of the last memory transfer, 12-byte form (status in bytes 0..2,
/// fault address after it); preferred when the probe supports it.
pub const DEBUG_APIV2_GETLASTRWSTATUS2: u8 = 0x3E;
/// Transport selector for [`DEBUG_APIV2_ENTER`]: SWD.
pub const DEBUG_ENTER_SWD: u8 = 0xA3;

/// STMicroelectronics' USB vendor ID.
pub const USB_VID_ST: u16 = 0x0483;
/// Original ST-LINK/V2 (the probe on the tag bases; OUT endpoint 2).
pub const USB_PID_STLINK_32L: u16 = 0x3748;
/// ST-LINK/V2 with audio interface (V2-1 protocol; OUT endpoint 1).
pub const USB_PID_STLINK_32L_AUDIO: u16 = 0x374a;
/// ST-LINK/V2-1 as fitted to Nucleo boards.
pub const USB_PID_STLINK_NUCLEO: u16 = 0x374b;
/// ST-LINK/V2-1 without mass storage.
pub const USB_PID_STLINK_V2_1: u16 = 0x3752;
/// ST-LINK/V3E.
pub const USB_PID_STLINK_V3E: u16 = 0x374e;
/// ST-LINK/V3 (V3SET and similar).
pub const USB_PID_STLINK_V3S: u16 = 0x374f;
/// ST-LINK/V3 with two virtual COM ports. Not in [`super::is_stlink`]'s
/// accepted list, so it is opened only when a [`super::UsbDev`] names it
/// explicitly; its endpoints are known for that case.
pub const USB_PID_STLINK_V3_2VCP: u16 = 0x3753;
/// ST-LINK/V3PWR.
pub const USB_PID_STLINK_V3_PWR: u16 = 0x3757;

/// Timeout for one USB bulk transfer (C++ `usb_xfer` uses the same 1.5 s).
///
/// Long enough for any single ST-LINK command; monitor-level waits are made
/// of many short commands, never one long transfer.
const USB_TRANSFER_TIMEOUT: Duration = Duration::from_millis(1500);

// The tag bases' identify command and reply; the firmware side is
// STLINK_TAGBASE_* in embedded/bases/common/inc/stlink.h.
/// Top-level command byte reserved for the tag bases' own extensions.
const TAGBASE_COMMAND: u8 = 0xF6;
/// Tag-base sub-command: identify the base board and its firmware.
const TAGBASE_IDENTIFY: u8 = 0x99;
/// Length in bytes of the identify reply: a 4-byte header (`80 00`, format,
/// reserved) and a NUL-padded text field.
const TAGBASE_IDENTIFY_LEN: usize = 128;
/// Identify reply format this host understands (byte 2 of the reply).
const TAGBASE_IDENTIFY_FORMAT: u8 = 1;
/// A base answers at once; a probe that does not know the command may never.
const TAGBASE_IDENTIFY_TIMEOUT: Duration = Duration::from_millis(300);

/// Decodes a little-endian `u16` from the first two bytes of `buf`.
///
/// # Panics
///
/// If `buf` is shorter than 2 bytes; callers pass fixed-size reply buffers.
pub(super) fn unpack16(buf: &[u8]) -> u16 {
    u16::from_le_bytes([buf[0], buf[1]])
}

/// Decodes a little-endian `u32` from the first four bytes of `buf`.
///
/// # Panics
///
/// If `buf` is shorter than 4 bytes; callers pass fixed-size reply buffers.
pub(super) fn unpack32(buf: &[u8]) -> u32 {
    u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]])
}

/// The ST-LINK mode code: the low byte of a GET_CURRENT_MODE reply.
pub(super) fn mode_code(mode: u16) -> u8 {
    (mode & 0xff) as u8
}

/// A debug command block: `F2 <sub> <addr:le32>`, the rest zero.
///
/// Callers patch further bytes (count, value, nRST level) into the returned
/// block before sending it.
pub(super) fn debug_cmd(sub: u8, addr: u32) -> [u8; 16] {
    let mut cmd = [0u8; 16];
    cmd[0] = DEBUG_COMMAND;
    cmd[1] = sub;
    cmd[2..6].copy_from_slice(&addr.to_le_bytes());
    cmd
}

/// Clears a halt on a bulk endpoint that has just answered a transfer with a
/// stall (`LIBUSB_ERROR_PIPE`), so the caller can try the transfer once more.
///
/// A base whose firmware predates the identify command (`F6 99`; a 2022
/// bittag-base-v7 build) was seen to leave its endpoint stalled after that
/// command: every later command failed with `LIBUSB_ERROR_PIPE` until the
/// base was unplugged, since nothing cleared the halt. Clearing it is done
/// only after a stall, never on open, because a `CLEAR_FEATURE` resets the
/// data toggle and a healthy base's USB stack is not relied on to follow.
///
/// Returns true if the halt was cleared. Logs a warning either way, so a base
/// that stalls is visible in the log.
fn clear_stall(handle: &rusb::DeviceHandle<rusb::Context>, endpoint: u8, dir: &str) -> bool {
    match handle.clear_halt(endpoint) {
        Ok(()) => {
            warn!("bulk {dir} endpoint {endpoint:#04x} stalled; cleared the halt, retrying once");
            true
        }
        Err(e) => {
            warn!(
                "bulk {dir} endpoint {endpoint:#04x} stalled, and clearing it failed: {}",
                libusb_name(&e)
            );
            false
        }
    }
}

impl LinkAdapt {
    /// Reads exactly `buf.len()` bytes from the bulk IN endpoint.
    ///
    /// Blocks for up to [`USB_TRANSFER_TIMEOUT`]. Returns false, with the
    /// libusb error or the short count logged, if no probe is open, the
    /// transfer fails, or fewer bytes arrive than asked for. A failure is
    /// logged through the failure run (see [`LinkAdapt::note_failure`]),
    /// and `LIBUSB_ERROR_NO_DEVICE` closes the probe (see
    /// [`transfer_failed`](Self::transfer_failed)). A stalled endpoint
    /// (`LIBUSB_ERROR_PIPE`) is cleared and the transfer tried once more;
    /// see [`clear_stall`]. Half of the C++ `usb_xfer`.
    fn usb_in(&mut self, buf: &mut [u8]) -> bool {
        let Some(handle) = &self.handle else {
            self.no_device_open();
            return false;
        };
        let start = Instant::now();
        let mut result = handle.read_bulk(self.ep_in, buf, USB_TRANSFER_TIMEOUT);
        if matches!(result, Err(rusb::Error::Pipe)) && clear_stall(handle, self.ep_in, "IN") {
            result = handle.read_bulk(self.ep_in, buf, USB_TRANSFER_TIMEOUT);
        }
        let res = match result {
            Ok(n) => n,
            Err(e) => {
                self.transfer_failed(&e);
                return false;
            }
        };
        if INSTRUMENTED {
            self.stats.usb_in_calls += 1;
            self.stats.usb_in_bytes += res as u64;
            self.stats.usb_in_ns += elapsed_ns(start);
        }
        if res != buf.len() {
            let level = self.note_failure();
            log!(level, "Transferred {res} bytes instead of {}", buf.len());
            return false;
        }
        true
    }

    /// Writes all of `buf` to the bulk OUT endpoint.
    ///
    /// Blocks for up to [`USB_TRANSFER_TIMEOUT`]. Returns false, with the
    /// reason logged, if no probe is open, the transfer fails, or it is
    /// short. Failures, a stalled endpoint included, are logged and handled
    /// as in [`usb_in`](Self::usb_in). The other half of the C++ `usb_xfer`.
    fn usb_out(&mut self, buf: &[u8]) -> bool {
        let Some(handle) = &self.handle else {
            self.no_device_open();
            return false;
        };
        let start = Instant::now();
        let mut result = handle.write_bulk(self.ep_out, buf, USB_TRANSFER_TIMEOUT);
        if matches!(result, Err(rusb::Error::Pipe)) && clear_stall(handle, self.ep_out, "OUT") {
            result = handle.write_bulk(self.ep_out, buf, USB_TRANSFER_TIMEOUT);
        }
        let res = match result {
            Ok(n) => n,
            Err(e) => {
                self.transfer_failed(&e);
                return false;
            }
        };
        if INSTRUMENTED {
            self.stats.usb_out_calls += 1;
            self.stats.usb_out_bytes += res as u64;
            self.stats.usb_out_ns += elapsed_ns(start);
        }
        if res != buf.len() {
            let level = self.note_failure();
            log!(level, "Transferred {res} bytes instead of {}", buf.len());
            return false;
        }
        true
    }

    /// Logs a failed bulk transfer, under libusb's name for `e`, as one
    /// failure of the run.
    ///
    /// `LIBUSB_ERROR_NO_DEVICE` means the probe has gone (unplugged, or
    /// re-enumerated after a reset), and no later transfer on this handle
    /// can succeed: the probe is closed at once, without the exit-debug
    /// command it can no longer receive, and an ERROR saying so is logged
    /// whatever the run's length. [`is_attached`](LinkAdapt::is_attached)
    /// then reports false, which the monitor's poll loops check, so a call
    /// in progress ends instead of polling a missing device until it times
    /// out; every later call fails as "not attached" until a new attach.
    /// Any other error leaves the probe open: a timeout or a stall can be
    /// transient.
    fn transfer_failed(&mut self, e: &rusb::Error) {
        let level = self.note_failure();
        if *e == rusb::Error::NoDevice {
            error!(
                "{}: the probe has gone; closing it, attach again to continue",
                libusb_name(e)
            );
            self.handle = None;
            self.probe_lost = true;
        } else {
            log!(level, "{}", libusb_name(e));
        }
    }

    /// Logs a transfer attempted with no probe open.
    ///
    /// After the probe was lost (see [`transfer_failed`](Self::transfer_failed))
    /// this is the expected result of every later command in the same call,
    /// so it is counted in the failure run; otherwise it is a caller error
    /// and always logged at ERROR.
    fn no_device_open(&mut self) {
        if self.probe_lost {
            let level = self.note_failure();
            log!(level, "USB transfer with no device open (the probe was disconnected)");
        } else {
            error!("USB transfer with no device open");
        }
    }

    /// Sends a command block, then reads `buf.len()` bytes if non-empty.
    ///
    /// The C++ `cmd_eval` with `ReadnWrite` true. Returns false, with the
    /// failing phase logged at the failure run's level (see
    /// [`LinkAdapt::failure_level`]), if either transfer fails; `buf` is then
    /// unspecified. Does not interpret any status bytes in the reply.
    pub(super) fn cmd_read(&mut self, cmd: &[u8; 16], buf: &mut [u8]) -> bool {
        let start = Instant::now();
        if !self.usb_out(cmd) {
            log!(self.failure_level(), "cmd_eval request failed");
            return false;
        }
        if !buf.is_empty() && !self.usb_in(buf) {
            log!(self.failure_level(), "cmd_eval response failed");
            return false;
        }
        self.count_cmd(start);
        true
    }

    /// Sends a command block, then writes `buf` if non-empty.
    ///
    /// The C++ `cmd_eval` with `ReadnWrite` false. Returns false, with the
    /// failing phase logged, if either transfer fails; the command may then
    /// have been partly carried out by the probe.
    pub(super) fn cmd_write(&mut self, cmd: &[u8; 16], buf: &[u8]) -> bool {
        let start = Instant::now();
        if !self.usb_out(cmd) {
            log!(self.failure_level(), "cmd_eval request failed");
            return false;
        }
        if !buf.is_empty() && !self.usb_out(buf) {
            log!(self.failure_level(), "cmd_eval response failed");
            return false;
        }
        self.count_cmd(start);
        true
    }

    /// Adds one completed command, begun at `start`, to the statistics.
    fn count_cmd(&mut self, start: Instant) {
        if INSTRUMENTED {
            self.stats.cmd_calls += 1;
            self.stats.cmd_ns += elapsed_ns(start);
        }
    }

    /// The probe's current mode word (see [`mode_code`]), or `None` if the
    /// command failed.
    pub(super) fn current_mode(&mut self) -> Option<u16> {
        let mut cmd = [0u8; 16];
        cmd[0] = GET_CURRENT_MODE;
        let mut result = [0u8; 2];
        self.cmd_read(&cmd, &mut result).then(|| unpack16(&result))
    }

    /// Leaves debug mode. The probe sends no reply, so true means only that
    /// the command block was sent.
    pub(super) fn exit_debug_mode(&mut self) -> bool {
        self.cmd_read(&debug_cmd(DEBUG_EXIT, 0), &mut [])
    }

    /// Sends the SWD enter command once and returns the probe's status.
    ///
    /// [`DEBUG_ERR_OK`] means the probe opened the target's debug port. On a
    /// tag base anything else means its `SWD_Open()` failed, and the base
    /// does not reopen the port on later debug-register reads, so every one
    /// of them would fail; [`enter_swd`](LinkAdapt::enter_swd) retries.
    /// `None` if the command did not complete over USB (logged by the
    /// transfer layer).
    pub(super) fn enter_debug_mode(&mut self) -> Option<u16> {
        let mut cmd = debug_cmd(DEBUG_APIV2_ENTER, 0);
        cmd[2] = DEBUG_ENTER_SWD;
        let mut result = [0u8; 2];
        if !self.cmd_read(&cmd, &mut result) {
            return None;
        }
        Some(unpack16(&result))
    }

    /// Leaves the probe's DFU mode. No reply; true means the block was sent.
    pub(super) fn exit_dfu_mode(&mut self) -> bool {
        let mut cmd = [0u8; 16];
        cmd[0] = DFU_COMMAND;
        cmd[1] = DFU_EXIT;
        self.cmd_read(&cmd, &mut [])
    }

    /// The status of the last memory transfer, [`DEBUG_ERR_OK`] on success.
    ///
    /// A memory read or write command reports success at the USB level even
    /// when the target faulted the access, so every transfer is confirmed
    /// with this. Tries the 12-byte form first and falls back to the 2-byte
    /// form for older probe firmware; `None` if both fail.
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

impl LinkAdapt {
    /// Asks the base which board and firmware it is, with the tag bases' own
    /// `F6 99` command (port of `LinkAdapt::BaseIdentity()`).
    ///
    /// A base built with the command answers with
    /// `base=<target>;fw=<git hash>;dirty=<0|1>;date=<commit date>;stlink=V2J37S7`.
    /// A base built before it answers with a 2-byte FAULT, or (2022 firmware)
    /// not at all, so the reply is read with a short timeout; both give
    /// `None`, as does a probe that is not open. Sends nothing to the target.
    ///
    /// Bypasses `cmd_read` (and the statistics) so that the
    /// short reply timeout applies and a missing reply is not logged as an
    /// error. Blocks for at most 1.5 s to send and 300 ms to receive.
    pub fn base_identity(&mut self) -> Option<String> {
        let handle = self.handle.as_ref()?;
        let mut cmd = [0u8; 16];
        cmd[0] = TAGBASE_COMMAND;
        cmd[1] = TAGBASE_IDENTIFY;
        if handle
            .write_bulk(self.ep_out, &cmd, USB_TRANSFER_TIMEOUT)
            .ok()?
            != cmd.len()
        {
            return None;
        }
        let mut reply = [0u8; TAGBASE_IDENTIFY_LEN];
        let n = match handle.read_bulk(self.ep_in, &mut reply, TAGBASE_IDENTIFY_TIMEOUT) {
            Ok(n) => n,
            Err(e) => {
                debug!(
                    "base identify: no reply ({}); not a tag base, or one that predates the command",
                    libusb_name(&e)
                );
                return None;
            }
        };
        if n != reply.len()
            || reply[0] != 0x80
            || reply[1] != 0
            || reply[2] != TAGBASE_IDENTIFY_FORMAT
        {
            debug!("base identify: {n}-byte reply; the base predates the command");
            return None;
        }
        let text = &reply[4..];
        let end = text.iter().position(|&b| b == 0).unwrap_or(text.len());
        Some(String::from_utf8_lossy(&text[..end]).into_owned())
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
