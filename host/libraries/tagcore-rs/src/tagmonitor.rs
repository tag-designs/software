//! The tag monitor protocol over an ST-LINK.
//!
//! Port of `tagmonitor.cc`. Two transports carry the same protobuf RPC:
//!
//! - **STM32L4**: the host writes a command word into DCRDR and pends the
//!   DebugMonitor exception through DEMCR; the firmware's handler runs the
//!   command and clears MON_REQ when the result is back in DCRDR.
//! - **STM32U3**: the host writes the command into the shared block at
//!   [`MONITOR_SHARED_ADDR`] and pends a reserved IRQ through the NVIC; the
//!   firmware publishes `status` and `result` in the same block.
//!
//! A protobuf RPC writes the serialised [`Req`] into the firmware's call
//! buffer, runs [`op::PROTOBUF`] with the request length, and reads back as
//! many bytes of [`Ack`] as the call returns.
//!
//! Attaching goes through reset on both targets, so it ends whatever the tag
//! was doing.

use std::fmt;
use std::thread::sleep;
use std::time::{Duration, Instant};

use log::{debug, error, warn};
use prost::Message;

use crate::linkadapt::{LinkAdapt, LinkAdaptStats, UsbDev, elapsed_ns};
use crate::monitor_abi::{
    DEBUGVERSION, MONITOR_CONNECTED_MAGIC, MONITOR_REQUEST_MAGIC, MONITOR_SHARED_ABI_VERSION,
    MONITOR_SHARED_ADDR, MONITOR_SHARED_FLAG_SESSION_READY, MONITOR_SHARED_KICK_ICPR_ADDR,
    MONITOR_SHARED_KICK_ISPR_ADDR, MONITOR_SHARED_KICK_MASK, MONITOR_SHARED_SIZE, MonitorShared,
    command_word, info, op, operation_name, status,
};
use crate::proto::{Ack, Req, req};

const INSTRUMENTED: bool = cfg!(feature = "instrumentation");

/// On a monitor call timeout, halt the core to read its registers for the
/// log (`TAGCORE_HALT_ON_MONITOR_TIMEOUT`).
const HALT_ON_MONITOR_TIMEOUT: bool = true;
/// Leave a core halted by that probe halted, for a debugger
/// (`TAGCORE_LEAVE_HALTED_ON_MONITOR_TIMEOUT`).
const LEAVE_HALTED_ON_MONITOR_TIMEOUT: bool = false;

// ARMv7-M / ARMv8-M debug and system control registers.
const DHCSR: u32 = 0xE000_EDF0;
const DCRSR: u32 = 0xE000_EDF4;
const DCRDR: u32 = 0xE000_EDF8;
const DEMCR: u32 = 0xE000_EDFC;

const SCB_ICSR: u32 = 0xE000_ED04;
const SCB_VTOR: u32 = 0xE000_ED08;
const SCB_AIRCR: u32 = 0xE000_ED0C;
const SCB_SCR: u32 = 0xE000_ED10;
const SCB_SHPR1: u32 = 0xE000_ED18;
const SCB_SHPR2: u32 = 0xE000_ED1C;
const SCB_SHPR3: u32 = 0xE000_ED20;
const SCB_SHCSR: u32 = 0xE000_ED24;
const SCB_CFSR: u32 = 0xE000_ED28;
const SCB_HFSR: u32 = 0xE000_ED2C;
const SCB_DFSR: u32 = 0xE000_ED30;
const SCB_MMFAR: u32 = 0xE000_ED34;
const SCB_BFAR: u32 = 0xE000_ED38;
const SCB_AFSR: u32 = 0xE000_ED3C;
const NVIC_IABR0: u32 = 0xE000_E304;

const STM32L4_DBGMCU_IDCODE: u32 = 0xE004_2000;
const STM32L4_RCC_CSR: u32 = 0x4002_1094;
const STM32U3_DBGMCU_IDCODE: u32 = 0xE004_4000;
const STM32U3_RCC_CSR: u32 = 0x4003_0D14;

// The flash empty check is an STM32L4 feature. On the STM32U3 FLASH_SR is at
// offset 0x20, so this address is FLASH_OPTKEYR there; gate on the device ID.
const L4_DBGMCU_DEV_ID_MASK: u32 = 0x0000_0FFF;
const L4_DEV_ID_L43X: u32 = 0x435;
const L4_FLASH_SR: u32 = 0x4002_2010;
const L4_FLASH_SR_PEMPTY: u32 = 1 << 17;

const DBGKEY: u32 = 0xA05F << 16;
const C_DEBUGEN: u32 = 1 << 0;
const C_HALT: u32 = 1 << 1;
const C_MASKINTS: u32 = 1 << 3;
const S_REGRDY: u32 = 1 << 16;
const S_HALT: u32 = 1 << 17;
const S_LOCKUP: u32 = 1 << 19;
const S_RESET_ST: u32 = 1 << 25;

/// DEMCR vector catch on core reset; also the "host attached" flag the L4
/// firmware reads.
const VC_CORERESET: u32 = 1;
const MON_REQ: u32 = 1 << 19;
const MON_PEND: u32 = 1 << 17;
const MON_EN: u32 = 1 << 16;

fn ms(n: u64) -> Duration {
    Duration::from_millis(n)
}

/// Counts and cumulative times of monitor RPCs, split by phase.
///
/// Every field stays zero, and `enabled` is false, when the crate is built
/// without the `instrumentation` feature.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TagMonitorStats {
    pub enabled: bool,
    pub rpc_calls: u64,
    pub request_bytes: u64,
    pub response_bytes: u64,
    pub rpc_total_ns: u64,
    pub serialize_ns: u64,
    pub write_request_ns: u64,
    pub monitor_call_ns: u64,
    pub read_response_ns: u64,
    pub parse_ns: u64,
}

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

impl fmt::Display for MemAccess {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.name())
    }
}

/// True if the IDCODE has a real device ID (neither all zero nor all one).
fn plausible_stm32_idcode(idcode: u32) -> bool {
    let device_id = idcode & 0xfff;
    device_id != 0 && device_id != 0xfff
}

/// True if `addr`/`length` are word multiples and the range does not wrap
/// past the top of the 32-bit address space.
pub(crate) fn valid_mem_range(addr: u32, length: usize) -> bool {
    addr & 3 == 0 && length & 3 == 0 && length as u64 <= u64::from(u32::MAX) - u64::from(addr) + 1
}

/// True if `[addr, addr + length)` overlaps the U3 monitor_shared block.
pub(crate) fn overlaps_monitor_shared(addr: u32, length: usize) -> bool {
    let first = u64::from(addr);
    let end = first + length as u64;
    let shared = u64::from(MONITOR_SHARED_ADDR);
    length != 0 && first < shared + u64::from(MONITOR_SHARED_SIZE) && end > shared
}

/// Rounds a byte count up to a whole number of words.
fn round4(n: usize) -> usize {
    (n + 3) & !3
}

/// Core registers read from a core halted for a timeout report.
#[derive(Default)]
struct HaltProbe {
    /// The probe, not something else, halted the core.
    halted_by_probe: bool,
    /// DHCSR after the halt request; S_HALT set if the core stopped.
    halted_dhcsr: u32,
    sp: u32,
    lr: u32,
    pc: u32,
    xpsr: u32,
    msp: u32,
    psp: u32,
    special: u32,
}

/// A tag monitor session over an ST-LINK.
///
/// Not thread-safe by itself: every method takes `&mut self`, which is what
/// serialises use of the SWD link. [`crate::Tag`] wraps one in a mutex.
pub struct TagMonitor {
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
    pub(crate) target_family: TargetFamily,
    target_idcode: u32,
    monitor_stats: TagMonitorStats,
}

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
    const RPCBUF_BYTES: usize = 64 * 1024;

    /// Creates a detached monitor with its own libusb session.
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

    /// The family detected at attach.
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

    /// Target supply voltage measured by the ST-LINK, in volts.
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

    fn clear_session(&mut self) {
        self.call_buf = 0;
        self.maxpacket = 0;
        self.sha_str = [0; 48];
        self.version = 0;
    }

    fn clear_all(&mut self) {
        self.clear_session();
        self.target_family = TargetFamily::Unknown;
        self.target_idcode = 0;
    }

    // ---- Word access helpers ----

    pub(crate) fn read_word(&mut self, addr: u32) -> Option<u32> {
        let mut bytes = [0u8; 4];
        self.link
            .read_mem32(addr, &mut bytes)
            .then(|| u32::from_le_bytes(bytes))
    }

    pub(crate) fn write_word(&mut self, addr: u32, value: u32) -> bool {
        self.link.write_mem32(addr, &value.to_le_bytes())
    }

    /// Reads a debug register, or 0 if the read fails; for diagnostics only.
    fn debug_or_zero(&mut self, addr: u32) -> u32 {
        self.link.read_debug32(addr).unwrap_or(0)
    }

    fn read_monitor_shared(&mut self) -> Option<MonitorShared> {
        let mut bytes = [0u8; MonitorShared::SIZE];
        self.link
            .read_mem32(MONITOR_SHARED_ADDR, &mut bytes)
            .then(|| MonitorShared::from_bytes(&bytes))
    }

    fn write_monitor_shared_word(&mut self, offset: u32, value: u32) -> bool {
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

    // ---- Live memory access ----

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

    // ---- Target identification ----

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

    /// Reads a core register of a halted core through DCRSR/DCRDR.
    fn read_core_register(&mut self, reg: u32) -> Option<u32> {
        for _ in 0..100 {
            if self
                .link
                .read_debug32(DHCSR)
                .is_some_and(|d| d & S_REGRDY != 0)
            {
                break;
            }
            sleep(ms(1));
        }
        if !self.link.write_debug32(DCRSR, reg) {
            return None;
        }
        for _ in 0..100 {
            if self
                .link
                .read_debug32(DHCSR)
                .is_some_and(|d| d & S_REGRDY != 0)
            {
                return self.link.read_debug32(DCRDR);
            }
            sleep(ms(1));
        }
        None
    }

    /// For a timeout report: unless the core is locked up, halts it and
    /// reads its registers. Does nothing when halting on timeout is off.
    fn halt_for_diagnostics(&mut self, dhcsr_snapshot: u32) -> HaltProbe {
        let mut probe = HaltProbe::default();
        if !HALT_ON_MONITOR_TIMEOUT || dhcsr_snapshot & S_LOCKUP != 0 {
            return probe;
        }
        probe.halted_by_probe = dhcsr_snapshot & S_HALT == 0;
        if !self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN | C_HALT) {
            return probe;
        }
        for _ in 0..100 {
            if let Some(d) = self.link.read_debug32(DHCSR) {
                probe.halted_dhcsr = d;
                if d & S_HALT != 0 {
                    break;
                }
            }
            sleep(ms(1));
        }
        if probe.halted_dhcsr & S_HALT != 0 {
            let mut reg = |r| self.read_core_register(r).unwrap_or(0);
            probe.sp = reg(13);
            probe.lr = reg(14);
            probe.pc = reg(15);
            probe.xpsr = reg(16);
            probe.msp = reg(17);
            probe.psp = reg(18);
            probe.special = reg(20);
        }
        probe
    }

    /// Reads the 8-word exception frame at `sp`, if readable.
    fn read_frame(&mut self, sp: u32) -> Option<[u32; 8]> {
        let mut bytes = [0u8; 32];
        if !self.link.read_mem32(sp, &mut bytes) {
            return None;
        }
        let mut frame = [0u32; 8];
        for (word, b) in frame.iter_mut().zip(bytes.chunks(4)) {
            *word = u32::from_le_bytes(b.try_into().unwrap());
        }
        Some(frame)
    }

    /// Clears DHCSR.C_MASKINTS on a core halted at its reset vector.
    ///
    /// C_MASKINTS survives a system reset and can change only while the core
    /// is halted, in a write that keeps C_HALT set; the write that releases
    /// the halt cannot clear it. An SWD tool that ran code with interrupts
    /// masked and did not clear it on exit leaves it set, and once an attach
    /// sets C_DEBUGEN the firmware stalls in its idle thread with every
    /// interrupt masked. Does nothing unless the bit is set and the core is
    /// halted, so an ordinary attach is unchanged.
    fn clear_stale_mask_ints(&mut self) {
        if let Some(dhcsr) = self.link.read_debug32(DHCSR)
            && dhcsr & C_MASKINTS != 0
            && dhcsr & S_HALT != 0
        {
            error!(
                "Monitor attach: DHCSR.C_MASKINTS was left set (dhcsr=0x{dhcsr:x}); clearing it"
            );
            self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN | C_HALT);
        }
    }

    // ---- Monitor calls ----

    /// Runs one monitor operation and returns its result word.
    ///
    /// [`op::MONITORSTOP`] returns 1 without waiting for the firmware to
    /// answer, because the tag may drop into low power as soon as it has seen
    /// the request. Returns `None`, with the reason logged, if not attached,
    /// the monitor is busy, the link fails or the firmware does not answer in
    /// time (500 ms for result reads; 2.5 s per call, or 65 s for a protobuf
    /// call on L4).
    fn call(&mut self, operation: u8, operand: i32) -> Option<u32> {
        if self.target_family == TargetFamily::Stm32U3 {
            return self.call_u3(operation, operand);
        }

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

    /// [`call`](Self::call) over the STM32U3 shared-block transport.
    fn call_u3(&mut self, operation: u8, operand: i32) -> Option<u32> {
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

    // ---- Attach and detach ----

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
    fn attach_l4(&mut self) -> bool {
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

    /// Starts a session on an STM32U3 target. On failure the session fields
    /// are cleared; the caller closes the probe.
    fn attach_u3(&mut self) -> bool {
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

    /// Opens the probe and starts a monitor session. **Resets the tag.**
    ///
    /// `usbdev` selects the probe as in [`LinkAdapt::attach`]. The probe holds
    /// nRST while it enters SWD; the target family is then read from the
    /// DBGMCU IDCODE and the matching transport's attach sequence releases
    /// reset into the monitor and reads its metadata (call buffer, its size,
    /// the git SHA). Returns false if already attached or any step fails, in
    /// which case the probe is closed and the reason logged.
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

    // ---- RPC ----

    /// Sends `req` to the firmware and returns its reply.
    ///
    /// Serialises the request into the firmware's call buffer, runs the
    /// PROTOBUF monitor call and decodes the reply, whatever its
    /// [`Ack::err`] code. Returns `None`, with the reason logged, if not
    /// attached, the request does not fit the call buffer, the link or
    /// monitor call fails, or the reply does not decode.
    pub fn rpc(&mut self, req: &Req) -> Option<Ack> {
        let rpc_start = Instant::now();

        if !self.is_attached() {
            error!("Monitor not attached");
            return None;
        }

        let size = req.encoded_len();
        if size > self.rpcbuf.len() {
            error!("Request message too big");
            return None;
        }
        let step = Instant::now();
        let mut out: &mut [u8] = &mut self.rpcbuf[..];
        if req.encode(&mut out).is_err() {
            error!("Request message too big");
            return None;
        }
        if INSTRUMENTED {
            self.monitor_stats.serialize_ns += elapsed_ns(step);
        }

        if size + 2 > self.maxpacket {
            error!("Input buffer too long");
            return None;
        }

        // The ST-LINK is faster for word-aligned access, and the firmware's
        // call buffer is word aligned, so whole words are transferred.
        let step = Instant::now();
        let write_ok = self
            .link
            .write_mem32(self.call_buf, &self.rpcbuf[..round4(size)]);
        if INSTRUMENTED {
            self.monitor_stats.write_request_ns += elapsed_ns(step);
        }
        if !write_ok {
            error!("RPC buffer write failed");
            return None;
        }

        let step = Instant::now();
        let retval = self.call(op::PROTOBUF, size as i32);
        if INSTRUMENTED {
            self.monitor_stats.monitor_call_ns += elapsed_ns(step);
        }
        let Some(retval) = retval else {
            error!("monitor_call failed");
            return None;
        };

        let len = retval as usize;
        if len > self.maxpacket {
            error!(
                "Reply length {len} exceeds the monitor buffer ({} bytes)",
                self.maxpacket
            );
            return None;
        }

        let step = Instant::now();
        let read_ok = len == 0
            || self
                .link
                .read_mem32(self.call_buf, &mut self.rpcbuf[..round4(len)]);
        if INSTRUMENTED {
            self.monitor_stats.read_response_ns += elapsed_ns(step);
        }
        if !read_ok {
            error!("Error reading RPC buffer");
            return None;
        }

        let step = Instant::now();
        let ack = Ack::decode(&self.rpcbuf[..len]);
        if INSTRUMENTED {
            self.monitor_stats.parse_ns += elapsed_ns(step);
            self.monitor_stats.rpc_calls += 1;
            self.monitor_stats.request_bytes += size as u64;
            self.monitor_stats.response_bytes += len as u64;
            self.monitor_stats.rpc_total_ns += elapsed_ns(rpc_start);
        }
        ack.map_err(|e| error!("Reply did not decode: {e}")).ok()
    }
}

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
