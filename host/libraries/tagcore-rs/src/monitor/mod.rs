//! The tag monitor protocol over an ST-LINK.
//!
//! Port of `tagmonitor.cc`. Two transports carry the same protobuf RPC:
//!
//! - **STM32L4** ([`l4`]): the host writes a command word into DCRDR and
//!   pends the DebugMonitor exception through DEMCR; the firmware's handler
//!   runs the command and clears MON_REQ when the result is back in DCRDR.
//! - **STM32U3** ([`u3`]): the host writes the command into the shared block
//!   at [`abi::MONITOR_SHARED_ADDR`] and pends a reserved IRQ through the
//!   NVIC; the firmware publishes `status` and `result` in the same block.
//!
//! A protobuf RPC ([`rpc`]) writes the serialised request into the
//! firmware's call buffer, runs [`abi::op::PROTOBUF`] with the request
//! length, and reads back as many bytes of reply as the call returns.
//!
//! [`TagMonitor`] is one type whose methods are spread over this folder:
//!
//! - `mod.rs` (here): the struct, its public API (attach, detach, memory
//!   access, keep-alive), target identification and the call dispatcher;
//! - [`cortex_m`]: Arm debug and system-control registers, and the core
//!   halt and register reads used in timeout reports;
//! - [`l4`] and [`u3`]: each transport's call, attach sequence and timeout
//!   report;
//! - [`rpc`]: protobuf framing;
//! - [`abi`]: the host/firmware monitor ABI from `include/monitor.h`;
//! - `rescue`: the legacy BitTag field-rescue helpers.
//!
//! Attaching goes through reset on both targets, so it ends whatever the tag
//! was doing.
//!
//! # Session lifecycle
//!
//! 1. [`TagMonitor::attach`] opens the probe with nRST held low, enters SWD,
//!    and reads DBGMCU_IDCODE to pick the [`TargetFamily`] (unknown falls
//!    back to the L4 transport).
//! 2. The family's attach sequence arms the debug block, releases reset into
//!    the firmware so that it boots seeing the host, and reads the monitor
//!    metadata: protocol version, call-buffer address and size, and the
//!    address of the git SHA string, which is read once and cached.
//! 3. RPCs ([`TagMonitor::rpc`]) and live memory access
//!    ([`TagMonitor::read_mem`], [`TagMonitor::write_mem`]) follow. The
//!    firmware ends the session on its own if the host goes quiet, so idle
//!    callers use [`TagMonitor::keep_alive`].
//! 4. [`TagMonitor::detach`] sends MONITORSTOP and closes the probe; the
//!    firmware then clears its own debug state and may enter low power.
//!
//! # Timeouts
//!
//! Waits are counted in poll iterations of one SWD read plus a 1 ms sleep,
//! as in the C++ code, so the nominal millisecond figures are lower bounds:
//! each poll is also a USB round trip. On STM32L4 a protobuf call polls up
//! to 65000 times (about 120 s in practice) and any other call 2500 times;
//! on STM32U3 every call except MONITORSTOP polls 2500 times. A timeout is
//! followed by a diagnostic report that may briefly halt the core; see
//! [`cortex_m`].
//!
//! # Threading
//!
//! Every method that touches the probe takes `&mut self`; there is no
//! internal locking. [`crate::Tag`] wraps one monitor in a mutex for shared
//! use. All calls block the caller, some for the full RPC timeout.
//!
//! The protocol is specified in `docs/shared/monitor-interface.md`; the
//! longer rationale for the crate's layering is in
//! `host/libraries/tagcore-rs/design/architecture.md`.

use std::fmt;
use std::thread::sleep;
use std::time::Duration;

use log::{debug, error};

use crate::link::{LinkAdapt, LinkAdaptStats, UsbDev};
use crate::proto::{Req, req};

pub mod abi;
pub mod cortex_m;
pub mod l4;
mod rescue;
pub mod rpc;
pub mod u3;

use abi::{MONITOR_SHARED_ADDR, MONITOR_SHARED_SIZE, MonitorShared, op};
use cortex_m::{DEMCR, MON_EN, MON_PEND, MON_REQ, VC_CORERESET};

/// Whether RPC statistics are gathered (the `instrumentation` feature).
const INSTRUMENTED: bool = cfg!(feature = "instrumentation");

/// STM32L4 DBGMCU_IDCODE (RM0394): device ID in bits 11..0 (0x435 for the
/// L43x/L44x), revision in bits 31..16. Read through the debug-register
/// path to identify the family; on a U3 the read fails or gives an
/// implausible value, so detection moves on to the U3 address.
const STM32L4_DBGMCU_IDCODE: u32 = 0xE004_2000;
/// STM32L4 RCC_CSR: reset-cause flags, logged in L4 timeout reports so a
/// reset during a session can be told apart from a hung handler.
const STM32L4_RCC_CSR: u32 = 0x4002_1094;
/// STM32U3 DBGMCU_IDCODE, at a different address from the L4's.
const STM32U3_DBGMCU_IDCODE: u32 = 0xE004_4000;
/// STM32U3 RCC_CSR: reset-cause flags, as [`STM32L4_RCC_CSR`] for the U3.
const STM32U3_RCC_CSR: u32 = 0x4003_0D14;

/// Shorthand for a [`Duration`] of `n` milliseconds.
fn ms(n: u64) -> Duration {
    Duration::from_millis(n)
}

/// Counts and cumulative times of monitor RPCs, split by phase.
///
/// Every field stays zero, and `enabled` is false, when the crate is built
/// without the `instrumentation` feature.
///
/// The counterpart of the C++ `TagMonitorStats`. Times are wall-clock
/// nanoseconds. The phase times are accumulated as each phase completes,
/// so an RPC that fails part-way adds to the earlier phases but not to
/// `rpc_calls`, the byte counts or `rpc_total_ns`, which only count RPCs
/// that reached the decode step.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TagMonitorStats {
    /// True when the crate was built with the `instrumentation` feature.
    pub enabled: bool,
    /// RPCs that got as far as decoding a reply (whether or not it decoded).
    pub rpc_calls: u64,
    /// Serialised request bytes, before rounding up to whole words.
    pub request_bytes: u64,
    /// Reply bytes the firmware reported, before rounding up to whole words.
    pub response_bytes: u64,
    /// End-to-end time of counted RPCs, in nanoseconds.
    pub rpc_total_ns: u64,
    /// Time spent encoding requests, in nanoseconds.
    pub serialize_ns: u64,
    /// Time spent writing requests into the call buffer, in nanoseconds.
    pub write_request_ns: u64,
    /// Time spent in the PROTOBUF monitor call (kick, firmware execution and
    /// polling), in nanoseconds; usually the dominant term.
    pub monitor_call_ns: u64,
    /// Time spent reading replies out of the call buffer, in nanoseconds.
    pub read_response_ns: u64,
    /// Time spent decoding replies, in nanoseconds.
    pub parse_ns: u64,
}

/// All counters zero, with `enabled` reflecting the build.
impl Default for TagMonitorStats {
    fn default() -> Self {
        Self {
            enabled: INSTRUMENTED,
            rpc_calls: 0,
            request_bytes: 0,
            response_bytes: 0,
            rpc_total_ns: 0,
            serialize_ns: 0,
            write_request_ns: 0,
            monitor_call_ns: 0,
            read_response_ns: 0,
            parse_ns: 0,
        }
    }
}

/// The MCU family on the far side of the probe, which selects the transport.
///
/// Detected at attach from DBGMCU_IDCODE (the L4 address is tried first)
/// and reset to [`TargetFamily::Unknown`] on detach.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum TargetFamily {
    /// Not attached, or the IDCODE matched neither family; the L4 transport
    /// is tried.
    #[default]
    Unknown,
    /// STM32L4: DebugMonitor/DCRDR transport.
    Stm32L4,
    /// STM32U3: shared-block transport.
    Stm32U3,
}

/// Prints `STM32L4`, `STM32U3` or `unknown`, as the C++ log lines do.
impl fmt::Display for TargetFamily {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            TargetFamily::Stm32L4 => "STM32L4",
            TargetFamily::Stm32U3 => "STM32U3",
            TargetFamily::Unknown => "unknown",
        })
    }
}

/// Outcome of [`TagMonitor::read_mem`] or [`TagMonitor::write_mem`].
///
/// Distinguishes the caller's mistakes from link failures so that a caller
/// such as a test server can report each one differently.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MemAccess {
    /// Every byte was transferred.
    Ok,
    /// No monitor session; the debug clocks may be off.
    NotAttached,
    /// Address or length not a multiple of 4, or the range wraps past
    /// 0xFFFFFFFF.
    InvalidRange,
    /// A write overlapped the U3 monitor_shared block without `force`.
    Protected,
    /// The ST-LINK transfer failed; part of the range may have been
    /// transferred.
    LinkError,
}

impl MemAccess {
    /// Short, stable name for logs and messages, e.g. `"ok"` or `"protected"`.
    pub fn name(self) -> &'static str {
        match self {
            MemAccess::Ok => "ok",
            MemAccess::NotAttached => "not attached",
            MemAccess::InvalidRange => "invalid range",
            MemAccess::Protected => "protected",
            MemAccess::LinkError => "link error",
        }
    }
}

/// Prints [`MemAccess::name`].
impl fmt::Display for MemAccess {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.name())
    }
}

/// True if the IDCODE has a real device ID (neither all zero nor all one).
///
/// A read from the wrong family's DBGMCU address, or from a target that is
/// not powered, returns a floating or zero bus value; this rejects those so
/// family detection falls through to the next candidate.
fn plausible_stm32_idcode(idcode: u32) -> bool {
    let device_id = idcode & 0xfff;
    device_id != 0 && device_id != 0xfff
}

/// True if `addr`/`length` are word multiples and the range does not wrap
/// past the top of the 32-bit address space.
///
/// `length` is in bytes. An empty range is valid anywhere aligned, and a
/// range may end exactly at 0x1_0000_0000.
pub(crate) fn valid_mem_range(addr: u32, length: usize) -> bool {
    addr & 3 == 0 && length & 3 == 0 && length as u64 <= u64::from(u32::MAX) - u64::from(addr) + 1
}

/// True if `[addr, addr + length)` overlaps the U3 monitor_shared block.
///
/// Half-open on both sides; an empty range never overlaps. The whole
/// reserved [`MONITOR_SHARED_SIZE`] is guarded, not just the bytes the
/// struct uses. Computed in 64 bits so it cannot overflow.
pub(crate) fn overlaps_monitor_shared(addr: u32, length: usize) -> bool {
    let first = u64::from(addr);
    let end = first + length as u64;
    let shared = u64::from(MONITOR_SHARED_ADDR);
    length != 0 && first < shared + u64::from(MONITOR_SHARED_SIZE) && end > shared
}

/// A tag monitor session over an ST-LINK.
///
/// Not thread-safe by itself: every method takes `&mut self`, which is what
/// serialises use of the SWD link. [`crate::Tag`] wraps one in a mutex.
///
/// Port of the C++ `TagMonitor` (`tagmonitor.*`), which inherits from
/// `LinkAdapt`; here the link is a field. The session fields below are
/// valid only while attached and are zeroed on detach or a failed attach.
/// See the [module docs](self) for the session lifecycle and timeouts.
pub struct TagMonitor {
    /// The probe. `pub(crate)` so [`crate::Tag`] can reach link-level calls
    /// such as [`LinkAdapt::base_identity`] without a pass-through here.
    pub(crate) link: LinkAdapt,
    /// Staging buffer for serialised requests and replies.
    rpcbuf: Vec<u8>,
    /// Address of the firmware's call buffer.
    call_buf: u32,
    /// Size of the firmware's call buffer in bytes.
    maxpacket: usize,
    /// The firmware's git SHA string, NUL padded.
    sha_str: [u8; 48],
    /// Monitor version the firmware reported.
    version: u32,
    /// The family detected at attach; selects the transport for every call.
    pub(crate) target_family: TargetFamily,
    /// The raw DBGMCU_IDCODE read at attach (0 if none was plausible), for
    /// log messages.
    target_idcode: u32,
    /// Counters for [`TagMonitor::monitor_stats`].
    monitor_stats: TagMonitorStats,
}

/// Same as [`TagMonitor::new`].
impl Default for TagMonitor {
    fn default() -> Self {
        Self::new()
    }
}

impl TagMonitor {
    /// Largest single ST-LINK transfer [`read_mem`](Self::read_mem) and
    /// [`write_mem`](Self::write_mem) issue.
    pub const MEM_CHUNK_BYTES: usize = 1024;

    /// Size of the host staging buffer, and so the largest call buffer a
    /// firmware may advertise.
    ///
    /// Both attach sequences reject a larger advertised buffer. One ST-LINK
    /// memory transfer carries at most 65535 bytes and the RPC path does not
    /// chunk, so a firmware advertising exactly this size could not return
    /// a reply that fills it.
    const RPCBUF_BYTES: usize = 64 * 1024;

    /// Creates a detached monitor with its own libusb session.
    ///
    /// Allocates the 64 KiB staging buffer; touches no device.
    pub fn new() -> Self {
        Self {
            link: LinkAdapt::new(),
            rpcbuf: vec![0; Self::RPCBUF_BYTES],
            call_buf: 0,
            maxpacket: 0,
            sha_str: [0; 48],
            version: 0,
            target_family: TargetFamily::Unknown,
            target_idcode: 0,
            monitor_stats: TagMonitorStats::default(),
        }
    }

    /// Lists the accepted ST-LINKs on USB; see [`LinkAdapt::available`].
    pub fn available(&self) -> Vec<UsbDev> {
        self.link.available()
    }

    /// True while the probe is open.
    pub fn is_attached(&self) -> bool {
        self.link.is_attached()
    }

    /// The family detected at attach; [`TargetFamily::Unknown`] when
    /// detached or when neither IDCODE was plausible.
    pub fn target_family(&self) -> TargetFamily {
        self.target_family
    }

    /// Zeroes the link statistics.
    pub fn reset_link_stats(&mut self) {
        self.link.reset_link_stats();
    }

    /// The link statistics; see [`LinkAdaptStats`].
    pub fn link_stats(&self) -> LinkAdaptStats {
        self.link.link_stats()
    }

    /// Zeroes the RPC statistics.
    pub fn reset_monitor_stats(&mut self) {
        self.monitor_stats = TagMonitorStats::default();
    }

    /// The RPC statistics; see [`TagMonitorStats`].
    pub fn monitor_stats(&self) -> TagMonitorStats {
        self.monitor_stats.clone()
    }

    /// Target supply voltage measured by the ST-LINK, in volts; see
    /// [`LinkAdapt::voltage`]. Needs an open probe but no monitor session.
    pub fn voltage(&mut self) -> Option<f32> {
        self.link.voltage()
    }

    /// The firmware's git SHA string read at attach, up to its first NUL.
    /// Empty when detached.
    pub fn git_sha_string(&self) -> String {
        let end = self
            .sha_str
            .iter()
            .position(|&b| b == 0)
            .unwrap_or(self.sha_str.len());
        String::from_utf8_lossy(&self.sha_str[..end]).into_owned()
    }

    /// Forgets the firmware's monitor metadata (call buffer, size, SHA and
    /// version), keeping the detected family.
    fn clear_session(&mut self) {
        self.call_buf = 0;
        self.maxpacket = 0;
        self.sha_str = [0; 48];
        self.version = 0;
    }

    /// Forgets the session metadata and the detected family and IDCODE.
    /// Touches neither the probe nor the tag.
    fn clear_all(&mut self) {
        self.clear_session();
        self.target_family = TargetFamily::Unknown;
        self.target_idcode = 0;
    }

    /// Reads one little-endian word of target memory through the
    /// memory-access path; `None` on any link failure (logged).
    pub(crate) fn read_word(&mut self, addr: u32) -> Option<u32> {
        let mut bytes = [0u8; 4];
        self.link
            .read_mem32(addr, &mut bytes)
            .then(|| u32::from_le_bytes(bytes))
    }

    /// Writes one little-endian word of target memory through the
    /// memory-access path; false on any link failure (logged).
    pub(crate) fn write_word(&mut self, addr: u32, value: u32) -> bool {
        self.link.write_mem32(addr, &value.to_le_bytes())
    }

    /// Reads a debug register, or 0 if the read fails; for diagnostics only.
    ///
    /// A 0 in a timeout report therefore means "zero or unreadable".
    fn debug_or_zero(&mut self, addr: u32) -> u32 {
        self.link.read_debug32(addr).unwrap_or(0)
    }

    /// Reads tag memory or peripheral space while the tag runs.
    ///
    /// Uses ST-LINK 32-bit memory-access-port reads, which do not halt the
    /// core, so the firmware and its monitor keep running. The transfer is
    /// split into chunks of at most [`MEM_CHUNK_BYTES`](Self::MEM_CHUNK_BYTES).
    /// `addr` and `buf.len()` must be multiples of 4; an empty `buf` is a
    /// no-op. On [`MemAccess::LinkError`] `buf` is unchanged past the failing
    /// chunk.
    ///
    /// Requires a session: the firmware keeps the debug clocks running only
    /// while one is active. Reading some peripheral registers has side
    /// effects, such as clearing status flags or popping a FIFO.
    pub fn read_mem(&mut self, addr: u32, buf: &mut [u8]) -> MemAccess {
        if !self.is_attached() {
            return MemAccess::NotAttached;
        }
        if !valid_mem_range(addr, buf.len()) {
            return MemAccess::InvalidRange;
        }
        for (i, chunk) in buf.chunks_mut(Self::MEM_CHUNK_BYTES).enumerate() {
            let at = addr + (i * Self::MEM_CHUNK_BYTES) as u32;
            if !self.link.read_mem32(at, chunk) {
                return MemAccess::LinkError;
            }
        }
        MemAccess::Ok
    }

    /// Writes tag memory or peripheral space while the tag runs.
    ///
    /// Same access path and chunking as [`read_mem`](Self::read_mem). On
    /// STM32U3 a write overlapping the monitor_shared block is refused with
    /// [`MemAccess::Protected`] unless `force` is set, because changing that
    /// block can end the session; STM32L4 has no such block. On
    /// [`MemAccess::LinkError`] earlier chunks may already have been written.
    ///
    /// Nothing else is checked: a write can corrupt firmware state,
    /// reconfigure peripherals or flash, or reset the part.
    pub fn write_mem(&mut self, addr: u32, data: &[u8], force: bool) -> MemAccess {
        if !self.is_attached() {
            return MemAccess::NotAttached;
        }
        if !valid_mem_range(addr, data.len()) {
            return MemAccess::InvalidRange;
        }
        if !force
            && self.target_family == TargetFamily::Stm32U3
            && overlaps_monitor_shared(addr, data.len())
        {
            return MemAccess::Protected;
        }
        for (i, chunk) in data.chunks(Self::MEM_CHUNK_BYTES).enumerate() {
            let at = addr + (i * Self::MEM_CHUNK_BYTES) as u32;
            if !self.link.write_mem32(at, chunk) {
                return MemAccess::LinkError;
            }
        }
        MemAccess::Ok
    }

    /// Keeps the monitor session alive while the host is otherwise idle.
    ///
    /// The firmware ends a session when the host goes quiet: within one to
    /// two heartbeat periods (5-10 s) on STM32U3, after 60 s without a
    /// request on STM32L4. On U3 this sets `host_activity` with one SWD
    /// write, which runs no code on the tag; on L4 it issues a get_status
    /// RPC. Call at least every 2-3 s for U3. Returns false if not attached
    /// or the write or RPC failed.
    pub fn keep_alive(&mut self) -> bool {
        if !self.is_attached() {
            return false;
        }
        if self.target_family == TargetFamily::Stm32U3 {
            return self.write_monitor_shared_word(MonitorShared::HOST_ACTIVITY, 1);
        }
        // L4 re-arms its session timer only on a monitor request.
        let request = Req {
            payload: Some(req::Payload::GetStatus(Default::default())),
        };
        self.rpc(&request).is_some()
    }

    /// Identifies the target from DBGMCU_IDCODE, trying the L4 address and
    /// then the U3 address, and records the family and IDCODE.
    ///
    /// Returns false, leaving [`TargetFamily::Unknown`], if neither read
    /// gives a plausible IDCODE. A failed L4 read may log an ERROR from the
    /// link even on a healthy U3 target. Port of
    /// `TagMonitor::DetectTargetFamily`.
    fn detect_target_family(&mut self) -> bool {
        self.target_family = TargetFamily::Unknown;
        self.target_idcode = 0;
        for (addr, family) in [
            (STM32L4_DBGMCU_IDCODE, TargetFamily::Stm32L4),
            (STM32U3_DBGMCU_IDCODE, TargetFamily::Stm32U3),
        ] {
            if let Some(idcode) = self.link.read_debug32(addr)
                && plausible_stm32_idcode(idcode)
            {
                self.target_family = family;
                self.target_idcode = idcode;
                return true;
            }
        }
        false
    }

    /// RCC_CSR (reset flags) for the detected family: (address, value, ok).
    ///
    /// For timeout reports only. On an unknown family or a failed read the
    /// value is 0xffffffff and `ok` false; the address is 0 for an unknown
    /// family.
    fn read_target_rcc_csr(&mut self) -> (u32, u32, bool) {
        let addr = match self.target_family {
            TargetFamily::Stm32L4 => STM32L4_RCC_CSR,
            TargetFamily::Stm32U3 => STM32U3_RCC_CSR,
            TargetFamily::Unknown => return (0, 0xffff_ffff, false),
        };
        match self.read_word(addr) {
            Some(value) => (addr, value, true),
            None => (addr, 0xffff_ffff, false),
        }
    }

    /// Runs one monitor operation over the detected family's transport and
    /// returns its result word: [`call_u3`](Self::call_u3) on an STM32U3,
    /// [`call_l4`](Self::call_l4) otherwise. `None`, with the reason logged,
    /// if the call fails.
    ///
    /// `operation` is one of [`abi::op`]; `operand` is packed above it by
    /// [`abi::command_word`]. Blocks until the firmware answers or the
    /// transport's poll limit runs out. Port of `TagMonitor::Call`.
    fn call(&mut self, operation: u8, operand: i32) -> Option<u32> {
        if self.target_family == TargetFamily::Stm32U3 {
            self.call_u3(operation, operand)
        } else {
            self.call_l4(operation, operand)
        }
    }

    /// Opens the probe and starts a monitor session. **Resets the tag.**
    ///
    /// `usbdev` selects the probe as in [`LinkAdapt::attach`]. The probe holds
    /// nRST while it enters SWD; the target family is then read from the
    /// DBGMCU IDCODE and the matching transport's attach sequence releases
    /// reset into the monitor and reads its metadata (call buffer, its size,
    /// the git SHA). Returns false if already attached or any step fails, in
    /// which case the probe is closed and the reason logged.
    ///
    /// **Never attach to a tag mid-run just to see what it is doing**: the
    /// reset ends the run. Blocks for tens of milliseconds on a healthy tag
    /// and up to several seconds when the firmware does not answer (the L4
    /// path retries once after a fresh reset pulse). Port of
    /// `TagMonitor::Attach`.
    pub fn attach(&mut self, usbdev: &UsbDev) -> bool {
        if self.is_attached() {
            error!("Already attached");
            return false;
        }
        self.clear_all();

        if !self.link.attach(usbdev) {
            error!("Attach failed");
            return false;
        }
        sleep(ms(20));

        self.detect_target_family();
        debug!(
            "Target family: {} idcode=0x{:x}",
            self.target_family, self.target_idcode
        );

        let attached = if self.target_family == TargetFamily::Stm32U3 {
            self.attach_u3()
        } else {
            self.attach_l4()
        };
        if attached {
            return true;
        }
        self.link.detach();
        self.detach();
        false
    }

    /// Ends the monitor session and closes the probe.
    ///
    /// Sends MONITORSTOP, after which the firmware clears its own debug state
    /// and may enter low power at once; on an identified target the host does
    /// no further debug-register cleanup, so as not to race it. Detaching
    /// when not attached only clears the session fields.
    ///
    /// On an unknown family the host clears DEMCR's monitor and vector-catch
    /// bits itself. A failed MONITORSTOP is logged but does not stop the
    /// probe from being closed; the firmware then ends the session itself
    /// once the host goes quiet. Never fails and is safe to call twice. Port
    /// of `TagMonitor::Detach`.
    pub fn detach(&mut self) {
        if !self.is_attached() {
            self.clear_all();
            return;
        }

        let stop = self.call(op::MONITORSTOP, 0);
        if self.target_family == TargetFamily::Stm32U3 {
            if let Some(s) = self.read_monitor_shared() {
                debug!(
                    "U3 monitor after MONITORSTOP request=0x{:x} flags=0x{:x} status=0x{:x} \
                     command=0x{:x} heartbeat=0x{:x} watchdog_ticks={} stop_ok={} \
                     stop_success={}",
                    s.request,
                    s.flags,
                    s.status,
                    s.command,
                    s.host_activity,
                    s.watchdog_ticks,
                    u8::from(stop.is_some()),
                    stop.unwrap_or(0)
                );
            }
            if !matches!(stop, Some(s) if s != 0) {
                error!("U3 Monitor Stop failed during detach");
            }
        } else if !matches!(stop, Some(s) if s != 0) {
            error!("Monitor Stop failed during detach");
        }

        if self.target_family != TargetFamily::Unknown {
            debug!(
                "{} monitor detach leaves post-stop debug cleanup to target firmware",
                self.target_family
            );
        } else if let Some(demcr) = self.link.read_debug32(DEMCR) {
            let monitor_bits = VC_CORERESET | MON_PEND | MON_REQ | MON_EN;
            if !self.link.write_debug32(DEMCR, demcr & !monitor_bits) {
                error!("Monitor detach failed to clear debug control register");
            } else if let Some(verify) = self.link.read_debug32(DEMCR)
                && verify & monitor_bits != 0
            {
                error!("Monitor detach left debug control bits set demcr=0x{verify:x}");
            }
        } else {
            error!("Monitor detach failed to read debug control register");
        }

        self.link.detach();
        self.clear_all();
    }
}

/// Detaches, so a dropped monitor ends the session cleanly.
impl Drop for TagMonitor {
    fn drop(&mut self) {
        self.detach();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mem_range_must_be_word_aligned_and_not_wrap() {
        assert!(valid_mem_range(0x2000_0000, 0));
        assert!(valid_mem_range(0x2000_0000, 1024));
        assert!(!valid_mem_range(0x2000_0002, 4));
        assert!(!valid_mem_range(0x2000_0000, 6));
        assert!(valid_mem_range(0xFFFF_FFFC, 4));
        assert!(!valid_mem_range(0xFFFF_FFFC, 8));
        assert!(valid_mem_range(0, 1 << 32));
    }

    #[test]
    fn shared_block_overlap_is_half_open() {
        let end = MONITOR_SHARED_ADDR + MONITOR_SHARED_SIZE;
        assert!(overlaps_monitor_shared(MONITOR_SHARED_ADDR, 4));
        assert!(overlaps_monitor_shared(MONITOR_SHARED_ADDR - 4, 8));
        assert!(overlaps_monitor_shared(end - 4, 4));
        assert!(!overlaps_monitor_shared(end, 4));
        assert!(!overlaps_monitor_shared(MONITOR_SHARED_ADDR - 4, 4));
        assert!(!overlaps_monitor_shared(MONITOR_SHARED_ADDR, 0));
    }

    #[test]
    fn idcode_plausibility_rejects_floating_bus() {
        assert!(plausible_stm32_idcode(0x1000_6435));
        assert!(!plausible_stm32_idcode(0));
        assert!(!plausible_stm32_idcode(0xffff_ffff));
        assert!(!plausible_stm32_idcode(0x1234_5000));
    }

    #[test]
    fn mem_access_names_are_stable() {
        assert_eq!(MemAccess::Ok.to_string(), "ok");
        assert_eq!(MemAccess::Protected.name(), "protected");
        assert_eq!(MemAccess::LinkError.name(), "link error");
    }

    #[test]
    fn detached_monitor_refuses_everything_without_touching_usb() {
        let mut monitor = TagMonitor::new();
        let mut buf = [0u8; 8];
        assert_eq!(
            monitor.read_mem(0x2000_0000, &mut buf),
            MemAccess::NotAttached
        );
        assert_eq!(
            monitor.write_mem(0x2000_0000, &buf, false),
            MemAccess::NotAttached
        );
        assert!(!monitor.keep_alive());
        assert!(monitor.rpc(&Req::default()).is_none());
        assert_eq!(monitor.git_sha_string(), "");
        monitor.detach();
        assert_eq!(monitor.target_family(), TargetFamily::Unknown);
    }

    #[test]
    fn git_sha_stops_at_first_nul() {
        let mut monitor = TagMonitor::new();
        monitor.sha_str[..7].copy_from_slice(b"aaa9557");
        assert_eq!(monitor.git_sha_string(), "aaa9557");
    }
}
