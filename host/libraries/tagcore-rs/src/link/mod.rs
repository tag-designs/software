//! ST-LINK access over libusb: discovery, attach, and the debug commands the
//! monitor needs.
//!
//! Port of `linkadapt.cc`. Every ST-LINK command is a 16-byte command block
//! sent on the OUT endpoint, optionally followed by a data phase in either
//! direction. Memory accesses use the 32-bit memory-access-port commands,
//! which do not halt the core, and are confirmed with a last-R/W-status read.
//!
//! [`LinkAdapt`] is one type whose methods are spread over this folder by
//! concern:
//!
//! - `mod.rs` (here): the struct, its statistics, attach and detach;
//! - [`discovery`]: finding ST-LINKs on USB ([`UsbDev`]) and opening one;
//! - `stlink`: the ST-LINK USB protocol: command blocks, transfers and mode
//!   changes;
//! - `target`: raw access to the target: memory, debug registers, nRST and
//!   voltage.

use std::time::Instant;

use log::{debug, error};
use rusb::{Context, DeviceHandle};

pub mod discovery;
mod stlink;
mod target;

use stlink::mode_code;

pub use discovery::{UsbDev, decode_serial, is_stlink};

/// Whether statistics are gathered (the `instrumentation` feature).
const INSTRUMENTED: bool = cfg!(feature = "instrumentation");

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

/// libusb's own name for an error, such as `LIBUSB_ERROR_TIMEOUT`, so log
/// lines read as the C++ tools' do (`libusb_error_name()`).
pub(crate) fn libusb_name(e: &rusb::Error) -> &'static str {
    use rusb::Error::*;
    match e {
        Io => "LIBUSB_ERROR_IO",
        InvalidParam => "LIBUSB_ERROR_INVALID_PARAM",
        Access => "LIBUSB_ERROR_ACCESS",
        NoDevice => "LIBUSB_ERROR_NO_DEVICE",
        NotFound => "LIBUSB_ERROR_NOT_FOUND",
        Busy => "LIBUSB_ERROR_BUSY",
        Timeout => "LIBUSB_ERROR_TIMEOUT",
        Overflow => "LIBUSB_ERROR_OVERFLOW",
        Pipe => "LIBUSB_ERROR_PIPE",
        Interrupted => "LIBUSB_ERROR_INTERRUPTED",
        NoMem => "LIBUSB_ERROR_NO_MEM",
        NotSupported => "LIBUSB_ERROR_NOT_SUPPORTED",
        BadDescriptor | Other => "LIBUSB_ERROR_OTHER",
    }
}

/// Nanoseconds since `start`, saturating at `u64::MAX`.
pub(crate) fn elapsed_ns(start: Instant) -> u64 {
    u64::try_from(start.elapsed().as_nanos()).unwrap_or(u64::MAX)
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
            .map_err(|e| error!("USB Init error {}", libusb_name(&e)))
            .ok();
        Self {
            ctx,
            handle: None,
            ep_in: 0,
            ep_out: 0,
            stats: LinkAdaptStats::default(),
        }
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

    /// Claims the opened probe and puts it in SWD debug mode.
    fn configure(&mut self, assert_reset: bool) -> bool {
        let Some(handle) = self.handle.as_mut() else {
            return false;
        };
        match handle.active_configuration() {
            Ok(1) => {}
            Ok(_) => {
                if let Err(e) = handle.set_active_configuration(1) {
                    error!("setting config failed: {}", libusb_name(&e));
                    return false;
                }
            }
            Err(e) => {
                error!("couldn't get config: {}", libusb_name(&e));
                return false;
            }
        }
        // Not supported on every platform (Windows), so the result is ignored.
        let _ = handle.set_auto_detach_kernel_driver(true);
        if let Err(e) = handle.claim_interface(0) {
            error!("claim interface failed {}", libusb_name(&e));
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
}

impl Drop for LinkAdapt {
    fn drop(&mut self) {
        self.detach();
    }
}
