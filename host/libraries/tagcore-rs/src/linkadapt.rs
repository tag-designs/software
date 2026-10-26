//! ST-LINK access over libusb: discovery, attach, and the debug commands the
//! monitor needs.
//!
//! Port of `linkadapt.cc`. Every ST-LINK command is a 16-byte command block
//! sent on the OUT endpoint, optionally followed by a data phase in either
//! direction. Memory accesses use the 32-bit memory-access-port commands,
//! which do not halt the core, and are confirmed with a last-R/W-status read.

use std::time::{Duration, Instant};

use log::{debug, error, warn};
use rusb::{Context, Device, DeviceDescriptor, DeviceHandle, UsbContext};

/// Timeout for one USB bulk transfer.
const USB_TRANSFER_TIMEOUT: Duration = Duration::from_millis(1500);

/// Whether statistics are gathered (the `instrumentation` feature).
const INSTRUMENTED: bool = cfg!(feature = "instrumentation");

/// ST-LINK USB command and status codes.
mod stlink {
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
}

/// One ST-LINK found on USB by [`LinkAdapt::available`], and the selector
/// [`LinkAdapt::attach`] matches against.
///
/// The default (all zero) value asks `attach` for the first ST-LINK found.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct UsbDev {
    /// USB vendor ID (0x0483 for ST).
    pub vid: u16,
    /// USB product ID; identifies the ST-LINK generation.
    pub pid: u16,
    /// USB bus number at enumeration.
    pub bus: u8,
    /// USB device address on `bus`; changes on replug.
    pub address: u8,
    /// The ST-LINK's USB serial number as printable text, or empty when it
    /// could not be read.
    ///
    /// The same string STM32_Programmer_CLI lists as `ST-LINK SN` and accepts
    /// as `sn=`. Old ST-LINK/V2 firmware reports a 12-byte binary serial,
    /// which is hex-encoded as the CLI shows it; any other unreadable serial
    /// is left empty. Not used by `attach`.
    pub serial: String,
}

/// Counts and cumulative times of the link's USB and ST-LINK operations.
///
/// Every field stays zero, and `enabled` is false, when the crate is built
/// without the `instrumentation` feature.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LinkAdaptStats {
    pub enabled: bool,
    pub usb_in_calls: u64,
    pub usb_out_calls: u64,
    pub usb_in_bytes: u64,
    pub usb_out_bytes: u64,
    pub usb_in_ns: u64,
    pub usb_out_ns: u64,
    pub cmd_calls: u64,
    pub cmd_ns: u64,
    pub read_mem_calls: u64,
    pub read_mem_bytes: u64,
    pub read_mem_ns: u64,
    pub write_mem_calls: u64,
    pub write_mem_bytes: u64,
    pub write_mem_ns: u64,
    pub read_debug_calls: u64,
    pub read_debug_ns: u64,
    pub write_debug_calls: u64,
    pub write_debug_ns: u64,
    pub rw_status_calls: u64,
    pub rw_status_ns: u64,
}

impl Default for LinkAdaptStats {
    fn default() -> Self {
        Self {
            enabled: INSTRUMENTED,
            usb_in_calls: 0,
            usb_out_calls: 0,
            usb_in_bytes: 0,
            usb_out_bytes: 0,
            usb_in_ns: 0,
            usb_out_ns: 0,
            cmd_calls: 0,
            cmd_ns: 0,
            read_mem_calls: 0,
            read_mem_bytes: 0,
            read_mem_ns: 0,
            write_mem_calls: 0,
            write_mem_bytes: 0,
            write_mem_ns: 0,
            read_debug_calls: 0,
            read_debug_ns: 0,
            write_debug_calls: 0,
            write_debug_ns: 0,
            rw_status_calls: 0,
            rw_status_ns: 0,
        }
    }
}

/// Nanoseconds since `start`, saturating at `u64::MAX`.
pub(crate) fn elapsed_ns(start: Instant) -> u64 {
    u64::try_from(start.elapsed().as_nanos()).unwrap_or(u64::MAX)
}

fn unpack16(buf: &[u8]) -> u16 {
    u16::from_le_bytes([buf[0], buf[1]])
}

fn unpack32(buf: &[u8]) -> u32 {
    u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]])
}

/// The ST-LINK mode code: the low byte of a GET_CURRENT_MODE reply.
fn mode_code(mode: u16) -> u8 {
    (mode & 0xff) as u8
}

/// A debug command block: `F2 <sub> <addr:le32>`, the rest zero.
fn debug_cmd(sub: u8, addr: u32) -> [u8; 16] {
    let mut cmd = [0u8; 16];
    cmd[0] = stlink::DEBUG_COMMAND;
    cmd[1] = sub;
    cmd[2..6].copy_from_slice(&addr.to_le_bytes());
    cmd
}

/// Reports whether a USB device is an ST-LINK probe the tools accept.
///
/// On Linux a user can open only the devices a udev rule grants, so
/// `host/linux/70-ultralight-tags.rules` lists these same products; keep the
/// two in step.
pub fn is_stlink(vid: u16, pid: u16) -> bool {
    use stlink::*;
    vid == USB_VID_ST
        && matches!(
            pid,
            USB_PID_STLINK_32L
                | USB_PID_STLINK_32L_AUDIO
                | USB_PID_STLINK_NUCLEO
                | USB_PID_STLINK_V2_1
                | USB_PID_STLINK_V3E
                | USB_PID_STLINK_V3S
                | USB_PID_STLINK_V3_PWR
        )
}

/// Decodes a raw USB string descriptor holding an ST-LINK serial number.
///
/// `raw` is the descriptor as read: `bLength`, `bDescriptorType`, then
/// UTF-16LE code units. A serial of printable ASCII is returned as is. Old
/// ST-LINK/V2 firmware reports its 12-byte serial as raw bytes, one per code
/// unit; that is returned as 24 upper-case hex digits, the form
/// STM32_Programmer_CLI lists and accepts (OpenOCD decodes it the same way).
/// Anything else, including a malformed descriptor, yields an empty string.
pub fn decode_serial(raw: &[u8]) -> String {
    const LIBUSB_DT_STRING: u8 = 0x03;
    if raw.len() < 4 || raw[1] != LIBUSB_DT_STRING {
        return String::new();
    }
    let units = raw.len().min(raw[0] as usize).saturating_sub(2) / 2;
    let code_units = || (0..units).map(|i| (raw[2 + 2 * i], raw[3 + 2 * i]));

    if code_units().all(|(lo, hi)| hi == 0 && (0x21..=0x7e).contains(&lo)) {
        return code_units().map(|(lo, _)| lo as char).collect();
    }
    if units != 12 {
        return String::new();
    }
    code_units().map(|(lo, _)| format!("{lo:02X}")).collect()
}

/// Reads an ST-LINK's serial number for [`UsbDev::serial`].
///
/// Opens the device only to read its string descriptor; no interface is
/// claimed and nothing is sent over SWD, so the tag on the base is not reset.
/// Returns an empty string when the serial is absent or cannot be read (for
/// example, the device is held by another driver).
fn read_serial(dev: &Device<Context>, desc: &DeviceDescriptor) -> String {
    let Some(index) = desc.serial_number_string_index() else {
        return String::new();
    };
    let Ok(handle) = dev.open() else {
        return String::new();
    };
    // GET_DESCRIPTOR(STRING, index), language en-US: what
    // libusb_get_string_descriptor() sends, keeping the raw bytes.
    let mut raw = [0u8; 256];
    let request_type = rusb::request_type(
        rusb::Direction::In,
        rusb::RequestType::Standard,
        rusb::Recipient::Device,
    );
    match handle.read_control(
        request_type,
        rusb::constants::LIBUSB_REQUEST_GET_DESCRIPTOR,
        (u16::from(rusb::constants::LIBUSB_DT_STRING) << 8) | u16::from(index),
        0x0409,
        &mut raw,
        Duration::from_millis(1000),
    ) {
        Ok(n) => decode_serial(&raw[..n]),
        Err(_) => String::new(),
    }
}

/// An ST-LINK opened in SWD debug mode.
///
/// Holds at most one open probe. Dropping it detaches. The memory and
/// debug-register methods require an attached probe and fail (with a logged
/// error) otherwise.
pub struct LinkAdapt {
    ctx: Option<Context>,
    handle: Option<DeviceHandle<Context>>,
    ep_in: u8,
    ep_out: u8,
    stats: LinkAdaptStats,
}

impl Default for LinkAdapt {
    fn default() -> Self {
        Self::new()
    }
}

impl LinkAdapt {
    /// Creates a libusb session. If libusb cannot be initialised the error is
    /// logged and every later call finds no devices.
    pub fn new() -> Self {
        let ctx = Context::new()
            .map_err(|e| error!("USB Init error {e}"))
            .ok();
        Self {
            ctx,
            handle: None,
            ep_in: 0,
            ep_out: 0,
            stats: LinkAdaptStats::default(),
        }
    }

    /// Lists the accepted ST-LINKs on USB, with their serial numbers.
    ///
    /// Does not open any probe for debug, so no tag is reset. Returns an
    /// empty list if enumeration fails (the failure is logged).
    pub fn available(&self) -> Vec<UsbDev> {
        let Some(ctx) = &self.ctx else {
            return Vec::new();
        };
        let devices = match ctx.devices() {
            Ok(devices) => devices,
            Err(e) => {
                error!("USB Init error {e}");
                return Vec::new();
            }
        };
        let mut stlinks = Vec::new();
        for dev in devices.iter() {
            let desc = match dev.device_descriptor() {
                Ok(desc) => desc,
                Err(e) => {
                    error!("USB Init error {e}");
                    break;
                }
            };
            if !is_stlink(desc.vendor_id(), desc.product_id()) {
                continue;
            }
            stlinks.push(UsbDev {
                vid: desc.vendor_id(),
                pid: desc.product_id(),
                bus: dev.bus_number(),
                address: dev.address(),
                serial: read_serial(&dev, &desc),
            });
        }
        stlinks
    }

    /// Opens an ST-LINK, holding the target in reset, and enters SWD mode.
    /// Same as [`attach_with_reset`](Self::attach_with_reset) with
    /// `assert_reset` set.
    pub fn attach(&mut self, usbdev: &UsbDev) -> bool {
        self.attach_with_reset(true, usbdev)
    }

    /// Opens an ST-LINK and enters SWD debug mode.
    ///
    /// With a non-zero `vid` and `pid` in `usbdev`, only the device with that
    /// vid, pid, bus and address matches; otherwise the first accepted
    /// ST-LINK does. Leaves DFU mode first if the probe is in it, and with
    /// `assert_reset` drives nRST low before entering SWD, so the target
    /// stays in reset until the caller releases it with
    /// [`assert_reset(true)`](Self::assert_reset).
    ///
    /// Returns false, with the probe closed, if nothing matched or any step
    /// failed. If the probe's mode cannot be read the probe is left open
    /// without entering SWD and true is returned, as the C++ code does.
    pub fn attach_with_reset(&mut self, assert_reset: bool, usbdev: &UsbDev) -> bool {
        self.handle = None;
        if self.open_matching(usbdev) && self.configure(assert_reset) {
            return true;
        }
        self.detach();
        false
    }

    /// Finds and opens the device `usbdev` selects, and sets the endpoints.
    fn open_matching(&mut self, usbdev: &UsbDev) -> bool {
        let Some(ctx) = &self.ctx else {
            return false;
        };
        let devices = match ctx.devices() {
            Ok(devices) => devices,
            Err(e) => {
                error!("USB Init error {e}");
                return false;
            }
        };
        for dev in devices.iter() {
            let desc = match dev.device_descriptor() {
                Ok(desc) => desc,
                Err(e) => {
                    error!("USB Init error {e}");
                    break;
                }
            };

            if usbdev.vid != 0 && usbdev.pid != 0 {
                if desc.vendor_id() != usbdev.vid
                    || desc.product_id() != usbdev.pid
                    || dev.bus_number() != usbdev.bus
                    || dev.address() != usbdev.address
                {
                    continue;
                }
            } else if !is_stlink(desc.vendor_id(), desc.product_id()) {
                continue;
            }

            let handle = match dev.open() {
                Ok(handle) => handle,
                Err(e) => {
                    error!("USB Open error {e}");
                    continue;
                }
            };

            // ST-LINK/V2 uses OUT endpoint 2; later probes use endpoint 1.
            self.ep_in = 1 | rusb::constants::LIBUSB_ENDPOINT_IN;
            self.ep_out = match desc.product_id() {
                stlink::USB_PID_STLINK_NUCLEO
                | stlink::USB_PID_STLINK_32L_AUDIO
                | stlink::USB_PID_STLINK_V2_1
                | stlink::USB_PID_STLINK_V3E
                | stlink::USB_PID_STLINK_V3S
                | stlink::USB_PID_STLINK_V3_2VCP
                | stlink::USB_PID_STLINK_V3_PWR => 1,
                _ => 2,
            } | rusb::constants::LIBUSB_ENDPOINT_OUT;

            let text = |index: Option<u8>| {
                index
                    .and_then(|i| handle.read_string_descriptor_ascii(i).ok())
                    .unwrap_or_default()
            };
            debug!(
                "Device Manufacturer: {}",
                text(desc.manufacturer_string_index())
            );
            debug!("Device Product: {}", text(desc.product_string_index()));
            debug!("Device Serial: {}", text(desc.serial_number_string_index()));
            debug!(
                "Device Bus {:03} Device {:03}",
                dev.bus_number(),
                dev.address()
            );

            self.handle = Some(handle);
            return true;
        }
        debug!("No matching devices found");
        false
    }

    /// Claims the opened probe and puts it in SWD debug mode.
    fn configure(&mut self, assert_reset: bool) -> bool {
        let Some(handle) = self.handle.as_mut() else {
            return false;
        };
        match handle.active_configuration() {
            Ok(1) => {}
            Ok(_) => {
                if let Err(e) = handle.set_active_configuration(1) {
                    error!("setting config failed: {e}");
                    return false;
                }
            }
            Err(e) => {
                error!("couldn't get config: {e}");
                return false;
            }
        }
        // Not supported on every platform (Windows), so the result is ignored.
        let _ = handle.set_auto_detach_kernel_driver(true);
        if let Err(e) = handle.claim_interface(0) {
            error!("claim interface failed {e}");
            return false;
        }
        debug!("Interface claimed !");

        if let Some(voltage) = self.voltage() {
            debug!("Target voltage {voltage:.2} V");
        }

        let Some(mode) = self.current_mode() else {
            return true;
        };
        debug!("ST-LINK mode before debug entry: 0x{mode:x}");
        if mode_code(mode) == stlink::DEV_DFU_MODE && !self.exit_dfu_mode() {
            error!("exit dfu mode failed");
            return false;
        }
        if assert_reset {
            if !self.assert_reset(false) {
                error!("asserting target reset failed");
                return false;
            }
            debug!("target reset asserted through ST-LINK");
        }
        if !self.enter_debug_mode() {
            error!("enter swd mode command failed");
            return false;
        }
        // Mode 4 is accepted as well as DEBUG_MODE for existing bases.
        match self.current_mode() {
            Some(mode) if matches!(mode_code(mode), 4 | stlink::DEV_DEBUG_MODE) => {
                debug!("ST-LINK mode after debug entry: 0x{mode:x}");
                true
            }
            mode => {
                error!("enter swd mode failed: 0x{:x}", mode.unwrap_or(0));
                false
            }
        }
    }

    /// Leaves SWD mode and closes the probe. Does nothing if not attached.
    pub fn detach(&mut self) {
        if self.handle.is_some() {
            self.exit_debug_mode();
        }
        self.handle = None;
    }

    /// True while a probe is open.
    pub fn is_attached(&self) -> bool {
        self.handle.is_some()
    }

    /// Zeroes the link statistics.
    pub fn reset_link_stats(&mut self) {
        self.stats = LinkAdaptStats::default();
    }

    /// The link statistics gathered since creation or the last reset.
    pub fn link_stats(&self) -> LinkAdaptStats {
        self.stats.clone()
    }

    // ---- USB transport ----

    fn usb_in(&mut self, buf: &mut [u8]) -> bool {
        let Some(handle) = &self.handle else {
            error!("USB transfer with no device open");
            return false;
        };
        let start = Instant::now();
        let res = match handle.read_bulk(self.ep_in, buf, USB_TRANSFER_TIMEOUT) {
            Ok(n) => n,
            Err(e) => {
                error!("{e}");
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
                error!("{e}");
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
    fn cmd_read(&mut self, cmd: &[u8; 16], buf: &mut [u8]) -> bool {
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
    fn cmd_write(&mut self, cmd: &[u8; 16], buf: &[u8]) -> bool {
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

    // ---- ST-LINK mode control ----

    fn current_mode(&mut self) -> Option<u16> {
        let mut cmd = [0u8; 16];
        cmd[0] = stlink::GET_CURRENT_MODE;
        let mut result = [0u8; 2];
        self.cmd_read(&cmd, &mut result).then(|| unpack16(&result))
    }

    fn exit_debug_mode(&mut self) -> bool {
        self.cmd_read(&debug_cmd(stlink::DEBUG_EXIT, 0), &mut [])
    }

    fn enter_debug_mode(&mut self) -> bool {
        let mut cmd = debug_cmd(stlink::DEBUG_APIV2_ENTER, 0);
        cmd[2] = stlink::DEBUG_ENTER_SWD;
        let mut result = [0u8; 2];
        if !self.cmd_read(&cmd, &mut result) {
            return false;
        }
        let status = unpack16(&result);
        if status != stlink::DEBUG_ERR_OK {
            warn!("enter swd mode reported status 0x{status:x}");
        }
        true
    }

    fn exit_dfu_mode(&mut self) -> bool {
        let mut cmd = [0u8; 16];
        cmd[0] = stlink::DFU_COMMAND;
        cmd[1] = stlink::DFU_EXIT;
        self.cmd_read(&cmd, &mut [])
    }

    fn get_rw_status(&mut self) -> Option<u16> {
        let start = Instant::now();
        let mut result = [0u8; 12];
        let status = if self.cmd_read(
            &debug_cmd(stlink::DEBUG_APIV2_GETLASTRWSTATUS2, 0),
            &mut result,
        ) {
            unpack16(&result)
        } else {
            // Older probe firmware has only the two-byte form.
            let mut legacy = [0u8; 2];
            if !self.cmd_read(
                &debug_cmd(stlink::DEBUG_APIV2_GETLASTRWSTATUS, 0),
                &mut legacy,
            ) {
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

    // ---- Target access, used by TagMonitor ----

    /// Target supply voltage in volts, as measured by the ST-LINK.
    pub(crate) fn voltage(&mut self) -> Option<f32> {
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
    pub(crate) fn read_mem32(&mut self, addr: u32, buf: &mut [u8]) -> bool {
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
    pub(crate) fn write_mem32(&mut self, addr: u32, data: &[u8]) -> bool {
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
    pub(crate) fn read_debug32(&mut self, addr: u32) -> Option<u32> {
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
            error!("read debug register 0x{addr:x} failed status=0x{status:x}");
            return None;
        }
        if INSTRUMENTED {
            self.stats.read_debug_calls += 1;
            self.stats.read_debug_ns += elapsed_ns(start);
        }
        Some(unpack32(&result[4..8]))
    }

    /// Writes one 32-bit word through the debug-register path.
    pub(crate) fn write_debug32(&mut self, addr: u32, val: u32) -> bool {
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
    pub(crate) fn assert_reset(&mut self, high: bool) -> bool {
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

impl Drop for LinkAdapt {
    fn drop(&mut self) {
        self.detach();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn descriptor(units: &[u16]) -> Vec<u8> {
        let mut raw = vec![(2 + 2 * units.len()) as u8, 0x03];
        for u in units {
            raw.extend_from_slice(&u.to_le_bytes());
        }
        raw
    }

    #[test]
    fn printable_serial_is_returned_as_is() {
        let units: Vec<u16> = "0670FF3".bytes().map(u16::from).collect();
        assert_eq!(decode_serial(&descriptor(&units)), "0670FF3");
    }

    #[test]
    fn old_stlink_v2_binary_serial_is_hex_encoded() {
        let units: Vec<u16> = (0..12).map(|i| [0x55, 0xff, 0x6b, 0x06][i % 4]).collect();
        assert_eq!(
            decode_serial(&descriptor(&units)),
            "55FF6B0655FF6B0655FF6B06"
        );
    }

    #[test]
    fn other_binary_or_malformed_serials_are_empty() {
        assert_eq!(decode_serial(&descriptor(&[0x01, 0x41, 0x42])), "");
        assert_eq!(decode_serial(&descriptor(&[0x0141; 11])), "");
        assert_eq!(decode_serial(&[4, 0x02, b'A', 0]), "");
        // Like the C++, any 12-unit binary serial is hex-encoded from the low bytes.
        assert_eq!(decode_serial(&descriptor(&[0x0141; 12])), "41".repeat(12));
        assert_eq!(decode_serial(&[2, 0x03]), "");
    }

    #[test]
    fn serial_respects_blength_shorter_than_transfer() {
        let mut raw = descriptor(&[u16::from(b'A'), u16::from(b'B')]);
        raw[0] = 4; // only "A" is inside the descriptor
        assert_eq!(decode_serial(&raw), "A");
    }

    #[test]
    fn only_accepted_stlink_products_match() {
        assert!(is_stlink(0x0483, 0x3748));
        assert!(is_stlink(0x0483, 0x3757));
        // V3 USB loader and the two-VCP V3 are deliberately not accepted.
        assert!(!is_stlink(0x0483, 0x374d));
        assert!(!is_stlink(0x0483, 0x3753));
        assert!(!is_stlink(0x1234, 0x3748));
    }

    #[test]
    fn debug_command_blocks_are_little_endian() {
        let cmd = debug_cmd(stlink::DEBUG_READMEM_32BIT, 0x2000_0010);
        assert_eq!(&cmd[..6], &[0xF2, 0x07, 0x10, 0x00, 0x00, 0x20]);
        assert!(cmd[6..].iter().all(|&b| b == 0));
    }
}
