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
//!
//! # Invariants
//!
//! - At most one probe is open per [`LinkAdapt`]; `handle` is `Some` exactly
//!   while attached, and every command first checks it (failing with a
//!   logged error rather than panicking).
//! - All calls block the calling thread on libusb bulk transfers, each with
//!   a 1.5 s timeout (300 ms for the base-identify reply). Nothing here
//!   retries; retry and polling policy belongs to the monitor layer.
//! - Errors are reported the way the C++ `LinkAdapt` reports them: the call
//!   returns `false`/`None` and the reason goes to the [`log`] facade,
//!   spelled with libusb's own error names so logs read the same as the C++
//!   tools'.
//!
//! # Threading
//!
//! [`LinkAdapt`] is `Send` but every method takes `&mut self` (or `&self`
//! only for enumeration), so the borrow checker serialises use of the
//! probe. The C++ class uses a recursive mutex for the same purpose;
//! [`crate::Tag`] puts the whole stack behind a [`std::sync::Mutex`].
//!
//! # Side effects on the tag
//!
//! Enumeration ([`LinkAdapt::available`]) never touches the target.
//! [`LinkAdapt::attach`] drives nRST low and leaves it there, so **the tag is
//! held in reset from attach until the caller releases it**; the monitor
//! layer does that as part of its own attach sequence. Detaching sends the
//! ST-LINK's exit-debug command and closes the probe.

use std::time::Instant;

use log::{debug, error};
use rusb::{Context, DeviceHandle};

pub mod discovery;
mod stlink;
mod target;

use stlink::mode_code;

pub use discovery::{UsbDev, decode_serial, is_stlink};

/// Whether statistics are gathered (the `instrumentation` feature).
///
/// A `const` rather than `#[cfg]` blocks so the counting code is always
/// type-checked; the optimiser removes it when the feature is off. Mirrors
/// `TAGCORE_ENABLE_INSTRUMENTATION` in the C++ build.
const INSTRUMENTED: bool = cfg!(feature = "instrumentation");

/// Counts and cumulative times of the link's USB and ST-LINK operations.
///
/// Every field stays zero, and `enabled` is false, when the crate is built
/// without the `instrumentation` feature.
///
/// The Rust counterpart of the C++ `LinkAdaptStats`. Counts include only
/// operations that succeeded (a failed transfer returns before it is
/// counted), and times are wall-clock nanoseconds measured with
/// [`std::time::Instant`], so they include libusb and OS scheduling
/// latency. The layers nest: one memory read is one command (two or more
/// USB transfers) plus one R/W-status command, and each is counted at its
/// own level, so the categories overlap and must not be summed.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LinkAdaptStats {
    /// True when the crate was built with the `instrumentation` feature,
    /// i.e. when the other fields mean anything.
    pub enabled: bool,
    /// Bulk IN (probe to host) transfers completed.
    pub usb_in_calls: u64,
    /// Bulk OUT (host to probe) transfers completed.
    pub usb_out_calls: u64,
    /// Bytes received on the IN endpoint.
    pub usb_in_bytes: u64,
    /// Bytes sent on the OUT endpoint, command blocks included.
    pub usb_out_bytes: u64,
    /// Total time spent in bulk IN transfers, in nanoseconds.
    pub usb_in_ns: u64,
    /// Total time spent in bulk OUT transfers, in nanoseconds.
    pub usb_out_ns: u64,
    /// ST-LINK commands completed (command block plus any data phase).
    pub cmd_calls: u64,
    /// Total time spent in ST-LINK commands, in nanoseconds.
    pub cmd_ns: u64,
    /// Successful [`LinkAdapt::read_mem32`] calls.
    pub read_mem_calls: u64,
    /// Bytes read by [`LinkAdapt::read_mem32`].
    pub read_mem_bytes: u64,
    /// Total time in [`LinkAdapt::read_mem32`], R/W-status check included,
    /// in nanoseconds.
    pub read_mem_ns: u64,
    /// Successful [`LinkAdapt::write_mem32`] calls.
    pub write_mem_calls: u64,
    /// Bytes written by [`LinkAdapt::write_mem32`].
    pub write_mem_bytes: u64,
    /// Total time in [`LinkAdapt::write_mem32`], R/W-status check included,
    /// in nanoseconds.
    pub write_mem_ns: u64,
    /// Successful [`LinkAdapt::read_debug32`] calls.
    pub read_debug_calls: u64,
    /// Total time in successful [`LinkAdapt::read_debug32`] calls, in
    /// nanoseconds.
    pub read_debug_ns: u64,
    /// Successful [`LinkAdapt::write_debug32`] calls.
    pub write_debug_calls: u64,
    /// Total time in successful [`LinkAdapt::write_debug32`] calls, in
    /// nanoseconds.
    pub write_debug_ns: u64,
    /// Last-R/W-status queries issued after memory transfers.
    pub rw_status_calls: u64,
    /// Total time spent in last-R/W-status queries, in nanoseconds.
    pub rw_status_ns: u64,
}

/// All counters zero, with `enabled` reflecting the build.
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
///
/// Shared with the monitor layer's RPC statistics.
pub(crate) fn elapsed_ns(start: Instant) -> u64 {
    u64::try_from(start.elapsed().as_nanos()).unwrap_or(u64::MAX)
}

/// An ST-LINK opened in SWD debug mode.
///
/// Holds at most one open probe. Dropping it detaches. The memory and
/// debug-register methods require an attached probe and fail (with a logged
/// error) otherwise.
///
/// Port of the C++ `LinkAdapt` (`linkadapt.*`). Unlike the C++ class, the
/// debug-register and memory calls are public rather than protected, so
/// tools such as `tag-monitor-test` can reach them without subclassing.
/// Methods take `&mut self`; see the [module docs](self) for threading and
/// for what attaching does to the tag.
pub struct LinkAdapt {
    /// The libusb session, or `None` if libusb failed to initialise (every
    /// later enumeration then finds nothing).
    ctx: Option<Context>,
    /// The open probe; `Some` exactly while attached.
    handle: Option<DeviceHandle<Context>>,
    /// Bulk IN endpoint address (endpoint 1, direction bit set).
    ep_in: u8,
    /// Bulk OUT endpoint address: endpoint 2 on the original ST-LINK/V2,
    /// endpoint 1 on V2-1 and later probes.
    ep_out: u8,
    /// Counters for [`LinkAdapt::link_stats`].
    stats: LinkAdaptStats,
    /// Debug-register reads failed in a row, so a burst is logged once.
    ///
    /// [`LinkAdapt::read_debug32`] logs the first failure of a run at ERROR,
    /// the rest at DEBUG, and a WARN with the count when a read succeeds
    /// again; callers poll DEMCR/DHCSR thousands of times per RPC, and a
    /// base can fail a burst of reads and then recover.
    debug_read_failures: u32,
}

/// Same as [`LinkAdapt::new`].
impl Default for LinkAdapt {
    fn default() -> Self {
        Self::new()
    }
}

impl LinkAdapt {
    /// Creates a libusb session. If libusb cannot be initialised the error is
    /// logged and every later call finds no devices.
    ///
    /// Opens no device; nothing is sent to any probe or tag.
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
            debug_read_failures: 0,
        }
    }

    /// Opens an ST-LINK, holding the target in reset, and enters SWD mode.
    /// Same as [`attach_with_reset`](Self::attach_with_reset) with
    /// `assert_reset` set.
    ///
    /// **The tag is left held in reset**: whatever it was doing has ended,
    /// and it stays stopped until [`assert_reset(true)`](Self::assert_reset)
    /// releases nRST. Port of `LinkAdapt::Attach(UsbDev)`.
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
    ///
    /// Any previously open probe is dropped (closed without the exit-debug
    /// command) before the new one is opened. Port of
    /// `LinkAdapt::Attach(bool, UsbDev)`.
    pub fn attach_with_reset(&mut self, assert_reset: bool, usbdev: &UsbDev) -> bool {
        self.handle = None;
        if self.open_matching(usbdev) && self.configure(assert_reset) {
            return true;
        }
        self.detach();
        false
    }

    /// Opens the probe `usbdev` selects and claims it, leaving DFU mode if
    /// it is in it, without touching nRST or entering SWD.
    ///
    /// For callers that order those steps themselves (a probe-rs driver
    /// asserts reset before entering SWD, as [`attach`](Self::attach) does).
    /// Returns false, with the probe closed, if nothing matched or the claim
    /// failed.
    pub fn open(&mut self, usbdev: &UsbDev) -> bool {
        self.handle = None;
        if self.open_matching(usbdev) && self.claim() {
            if let Some(mode) = self.current_mode()
                && mode_code(mode) == stlink::DEV_DFU_MODE
                && !self.exit_dfu_mode()
            {
                error!("exit dfu mode failed");
                self.detach();
                return false;
            }
            return true;
        }
        self.detach();
        false
    }

    /// Enters SWD debug mode on an open probe and checks that the probe
    /// reports it.
    ///
    /// Returns false, with the reason logged, if the enter command could not
    /// be sent or the probe then reports a mode other than debug. A non-OK
    /// status from the enter command itself is only warned about, as in the
    /// C++ code; the mode read-back is the real check. Leaves the probe open
    /// either way.
    pub fn enter_swd(&mut self) -> bool {
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

    /// Selects configuration 1 and claims interface 0 of the opened probe.
    ///
    /// Asks libusb to detach any kernel driver first (where the platform
    /// supports it), and logs the target voltage at DEBUG as a cheap sign of
    /// life. Returns false, with the reason logged, if no probe is open or
    /// configuration or claim fails; the caller closes the probe.
    fn claim(&mut self) -> bool {
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
        true
    }

    /// Claims the opened probe and puts it in SWD debug mode.
    ///
    /// The order matters and follows the C++ `Attach`: claim, read the mode,
    /// leave DFU if needed, drive nRST low (when `assert_reset`), and only
    /// then enter SWD, so the target is already in reset when the debug port
    /// comes up. Returns true without entering SWD when the mode cannot be
    /// read; see [`attach_with_reset`](Self::attach_with_reset).
    fn configure(&mut self, assert_reset: bool) -> bool {
        if !self.claim() {
            return false;
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
        self.enter_swd()
    }

    /// Leaves SWD mode and closes the probe. Does nothing if not attached.
    ///
    /// Sends no monitor command: ending a monitor session is
    /// [`crate::TagMonitor::detach`]'s job, and calling this directly on a
    /// tag with an open session leaves the firmware waiting for the host to
    /// go quiet before it ends the session itself.
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

/// Detaches, so a dropped link never leaves the probe in SWD mode.
impl Drop for LinkAdapt {
    fn drop(&mut self) {
        self.detach();
    }
}
