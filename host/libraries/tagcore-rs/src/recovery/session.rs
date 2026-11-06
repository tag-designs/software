//! The attach-without-boot SWD session.
//!
//! Ported from the deleted C++ `tagcore/recovery/swdsession.cc`, call for call, onto
//! [`LinkAdapt`]: the same register writes in the same order, because what a
//! capture shows depends on them. The sequence and its reasons are in
//! `host/libraries/tagcore-rs/design/swd-recovery.md`, "Attaching without
//! booting the firmware"; the rules that matter most are repeated on
//! [`Session::open`] and [`Session::close`].

use std::thread::sleep;
use std::time::{Duration, Instant};

use log::error;

use super::mcu::{ALL_MCUS, McuMap, find_mcu};
use crate::link::{LinkAdapt, LinkAdaptStats, UsbDev};

/// Debug Halting Control and Status Register.
pub(crate) const DHCSR: u32 = 0xE000_EDF0;
/// Debug Core Register Selector Register.
const DCRSR: u32 = 0xE000_EDF4;
/// Debug Core Register Data Register.
const DCRDR: u32 = 0xE000_EDF8;
/// Debug Exception and Monitor Control Register.
pub(crate) const DEMCR: u32 = 0xE000_EDFC;

/// The key every DHCSR write must carry in bits 31:16.
const DBGKEY: u32 = 0xA05F << 16;
/// DHCSR.C_DEBUGEN.
const C_DEBUGEN: u32 = 1 << 0;
/// DHCSR.C_HALT.
const C_HALT: u32 = 1 << 1;
/// DHCSR.C_MASKINTS: the debugger masks PendSV, SysTick and external
/// interrupts while the core runs.
const C_MASKINTS: u32 = 1 << 3;
/// DCRSR.REGWnR: the transfer is a write.
const REG_WNR: u32 = 1 << 16;
/// DHCSR.S_REGRDY: the last core-register transfer completed.
const S_REGRDY: u32 = 1 << 16;
/// DHCSR.S_HALT.
const S_HALT: u32 = 1 << 17;
/// DEMCR.VC_CORERESET: halt on the reset vector. Also the monitor's
/// attachment flag to the firmware (`MONCONNECTED`).
pub(crate) const VC_CORERESET: u32 = 1 << 0;
/// DEMCR.MON_EN.
pub(crate) const MON_EN: u32 = 1 << 16;

/// The base streams reads in 512-byte pieces and reports faults per request.
pub const BASE_PIECE: u32 = 512;
/// Largest transfer [`Session::read`] makes in one request.
pub const MAX_CHUNK: u32 = 32768;

/// Core register selector for R0 (DCRSR.REGSEL); R1 to R12 follow.
pub const REG_R0: u32 = 0;
/// Core register selector for the current stack pointer.
pub const REG_SP: u32 = 13;
/// Core register selector for LR.
pub const REG_LR: u32 = 14;
/// Core register selector for PC (the debug return address).
pub const REG_PC: u32 = 15;
/// Core register selector for xPSR.
pub const REG_XPSR: u32 = 16;
/// Core register selector for MSP.
pub const REG_MSP: u32 = 17;
/// Core register selector for PSP.
pub const REG_PSP: u32 = 18;

/// How [`Session::close`] leaves the tag.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum SwdExit {
    /// Disable debug and pulse NRST: the tag boots as after a plain
    /// connection.
    #[default]
    HardwareReset,
    /// Clear the vector catch but leave the core halted, for a follow-on
    /// step that needs it.
    LeaveHalted,
}

impl SwdExit {
    /// The name the capture manifest records: `"hardware_reset"` or
    /// `"leave_halted"`.
    pub fn manifest_name(self) -> &'static str {
        match self {
            SwdExit::HardwareReset => "hardware_reset",
            SwdExit::LeaveHalted => "leave_halted",
        }
    }
}

/// What [`Session::open`] observed, recorded for the capture manifest.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct AttachInfo {
    /// `DBGMCU_IDCODE`.
    pub idcode: u32,
    /// Target voltage reported by the base, in volts; 0 if it could not be
    /// read.
    pub voltage: f32,
    /// DHCSR before the session changed it.
    pub dhcsr_before: u32,
    /// DEMCR before; monitor bits here mean a monitor was left attached.
    pub demcr_before: u32,
    /// `DBGMCU_APB1FZR1` before.
    pub fz_before: u32,
    /// DHCSR once halted.
    pub dhcsr_halted: u32,
    /// PC once halted.
    pub pc: u32,
    /// The word at `flash_base + 4`.
    pub reset_vector: u32,
    /// The core reached S_HALT after reset release.
    pub halted: bool,
    /// PC equals the reset vector: no firmware ran.
    pub at_reset_vector: bool,
    /// The reset vector is erased, so nothing could have run.
    pub flash_blank: bool,
}

/// A range that could not be read: first byte, length in bytes.
pub type AddressRange = (u32, u32);

/// An SWD session that holds the tag's core at its reset vector.
///
/// Not thread-safe. Only one session, or one monitor session, may hold a
/// base at a time; opening a claimed base fails. Dropping an open session
/// closes it with [`SwdExit::HardwareReset`].
pub struct Session {
    /// The link to the base.
    link: LinkAdapt,
    /// The attached part, while open.
    mcu: Option<&'static McuMap>,
    /// What the attach observed.
    info: AttachInfo,
    /// True between a successful [`open`](Self::open) and
    /// [`close`](Self::close); also set while a failed open cleans up.
    open: bool,
}

impl Default for Session {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for Session {
    fn drop(&mut self) {
        self.close(SwdExit::HardwareReset);
    }
}

impl Session {
    /// A closed session; nothing is opened until [`open`](Self::open).
    pub fn new() -> Self {
        Session {
            link: LinkAdapt::new(),
            mcu: None,
            info: AttachInfo::default(),
            open: false,
        }
    }

    /// The bases on USB, as [`LinkAdapt::available`] lists them. Touches no
    /// tag.
    pub fn available(&self) -> Vec<UsbDev> {
        self.link.available()
    }

    /// The link's transfer statistics.
    pub fn link_stats(&self) -> LinkAdaptStats {
        self.link.link_stats()
    }

    /// Claims a base and stops the tag's core at its reset vector.
    ///
    /// With NRST asserted: identify the MCU from `DBGMCU_IDCODE`, read the
    /// target voltage, record DHCSR, DEMCR and the watchdog freeze register
    /// as found, write `DHCSR = DBGKEY | C_DEBUGEN | C_HALT` and
    /// `DEMCR = VC_CORERESET`, then release NRST. The core halts on the reset
    /// vector catch. PC must equal the reset vector read from flash (or the
    /// flash must be blank); otherwise the firmware has run and the open
    /// fails. On success the independent watchdog is frozen while the core
    /// is halted. The monitor bits are deliberately not set: the firmware
    /// must not see a monitor.
    ///
    /// # Errors
    ///
    /// Already open; no base, or the base held by another session or
    /// application; an unsupported or unreadable MCU; the halt could not be
    /// armed; or the core did not halt at its reset vector. On error the
    /// base is released and the tag left as a plain connection would leave
    /// it; [`attach_info`](Self::attach_info) still holds what was observed.
    ///
    /// # Warning
    ///
    /// The attach is a system reset. On a part whose option bytes erase SRAM
    /// on reset, that SRAM is erased by this call.
    pub fn open(&mut self, usbdev: &UsbDev) -> Result<(), String> {
        if self.open {
            return Err("SWD session already open".into());
        }
        self.info = AttachInfo::default();
        self.mcu = None;

        // NRST asserted, SWD entered. The claim fails if another session or
        // application holds the base.
        if !self.link.attach_with_reset(true, usbdev) {
            return Err("could not attach: no base, or the base is in use by another \
                        session or application"
                .into());
        }
        sleep(Duration::from_millis(20));

        for m in ALL_MCUS {
            if let Some(idcode) = self.link.read_debug32(m.dbgmcu_idcode)
                && plausible_idcode(idcode)
                && find_mcu(idcode) == Some(m)
            {
                self.mcu = Some(m);
                self.info.idcode = idcode;
                break;
            }
        }
        let Some(mcu) = self.mcu else {
            self.link.assert_reset(true);
            self.link.detach();
            return Err("unsupported or unreadable MCU".into());
        };

        self.info.voltage = self.link.voltage().unwrap_or(0.0);
        self.info.dhcsr_before = self.link.read_debug32(DHCSR).unwrap_or(0);
        self.info.demcr_before = self.link.read_debug32(DEMCR).unwrap_or(0);
        self.info.fz_before = self.link.read_debug32(mcu.dbgmcu_apb1fzr1).unwrap_or(0);

        // Arm the halt, then let the core out of reset into the vector catch.
        if !self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN | C_HALT)
            || !self.link.write_debug32(DEMCR, VC_CORERESET)
            || !self.link.assert_reset(true)
        {
            self.open = true;
            self.close(SwdExit::HardwareReset);
            return Err("could not arm the reset halt".into());
        }

        for _ in 0..200 {
            if let Some(d) = self.link.read_debug32(DHCSR) {
                self.info.dhcsr_halted = d;
                if d & S_HALT != 0 {
                    self.info.halted = true;
                    break;
                }
            }
            sleep(Duration::from_millis(1));
        }

        self.open = true;
        if self.info.halted {
            self.info.pc = self.read_core_register(REG_PC).unwrap_or(0);
            self.info.reset_vector = self.read_word(mcu.flash_base + 4).unwrap_or(0);
            self.info.flash_blank = self.info.reset_vector == 0xFFFF_FFFF;
            self.info.at_reset_vector = self.info.pc == (self.info.reset_vector & !1);
        }

        if !self.info.halted || !(self.info.at_reset_vector || self.info.flash_blank) {
            let why = format!(
                "core did not halt at its reset vector (halted={} pc=0x{:08x} \
                 vector=0x{:08x}): the firmware may have run",
                u8::from(self.info.halted),
                self.info.pc,
                self.info.reset_vector
            );
            error!("{why}");
            self.close(SwdExit::HardwareReset);
            return Err(why);
        }

        self.link
            .write_debug32(mcu.dbgmcu_apb1fzr1, self.info.fz_before | mcu.iwdg_freeze);
        Ok(())
    }

    /// Releases the tag and the base.
    ///
    /// Restores the watchdog freeze register, clears DHCSR.C_MASKINTS if a
    /// call left it set, clears DEMCR, then ends as `exit` says. Safe to call
    /// when not open.
    ///
    /// C_MASKINTS survives a system reset (only a power-on reset clears
    /// DHCSR) and can change only while halted, in a write that keeps C_HALT
    /// set. Left set, it is inert while C_DEBUGEN is clear but masks every
    /// interrupt as soon as a debugger -- the monitor -- sets C_DEBUGEN
    /// again, and the firmware stalls in its idle thread. A VC_CORERESET
    /// left set makes the next boot believe a monitor is attached, and the
    /// tag never sleeps. So both are cleared here, before the exit.
    pub fn close(&mut self, exit: SwdExit) {
        if !self.open {
            return;
        }
        if let Some(mcu) = self.mcu {
            self.link.write_debug32(mcu.dbgmcu_apb1fzr1, self.info.fz_before);
        }

        if let Some(d) = self.link.read_debug32(DHCSR)
            && d & C_MASKINTS != 0
        {
            if d & S_HALT == 0 {
                self.halt();
            }
            self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN | C_HALT);
            if let Some(d) = self.link.read_debug32(DHCSR)
                && d & C_MASKINTS != 0
            {
                error!(
                    "could not clear DHCSR.C_MASKINTS (dhcsr=0x{d:08x}): a later \
                     debugger connection will find interrupts masked"
                );
            }
        }

        match exit {
            SwdExit::HardwareReset => {
                // Hold the core in reset while debug is disabled, so it
                // comes out of reset with no vector catch and no halt
                // request, as after a plain connection.
                self.link.assert_reset(false);
                sleep(Duration::from_millis(5));
                self.link.write_debug32(DEMCR, 0);
                self.link.write_debug32(DHCSR, DBGKEY);
                self.link.assert_reset(true);
                sleep(Duration::from_millis(5));
            }
            SwdExit::LeaveHalted => {
                self.link.write_debug32(DEMCR, 0);
            }
        }

        self.link.detach();
        self.open = false;
    }

    /// True between a successful [`open`](Self::open) and
    /// [`close`](Self::close).
    pub fn is_open(&self) -> bool {
        self.open
    }

    /// The attached part's map, while open.
    pub fn mcu(&self) -> Option<&'static McuMap> {
        self.mcu
    }

    /// What [`open`](Self::open) observed.
    pub fn attach_info(&self) -> &AttachInfo {
        &self.info
    }

    /// Reads one 32-bit word through the debug-register path; `None` on an
    /// SWD fault.
    pub fn read_word(&mut self, addr: u32) -> Option<u32> {
        self.link.read_debug32(addr)
    }

    /// Writes one 32-bit word through the debug-register path; false on an
    /// SWD fault.
    pub fn write_word(&mut self, addr: u32, value: u32) -> bool {
        self.link.write_debug32(addr, value)
    }

    /// Reads `buf.len()` bytes at `addr`, continuing past faults.
    ///
    /// Reads in `chunk`-byte transfers (a multiple of 512, at most 32768;
    /// anything else means 4096). A transfer that faults is re-read in
    /// 512-byte pieces, the base's own transfer unit, so a fault is pinned to
    /// a small range: the bytes of a faulting piece are left zero and the
    /// piece is appended to `failed`. `addr` must be word aligned and the
    /// length a multiple of 4.
    ///
    /// Returns true when every byte was read.
    pub fn read(
        &mut self,
        addr: u32,
        buf: &mut [u8],
        chunk: u32,
        mut failed: Option<&mut Vec<AddressRange>>,
    ) -> bool {
        let chunk = if chunk == 0 || !chunk.is_multiple_of(BASE_PIECE) || chunk > MAX_CHUNK {
            4096
        } else {
            chunk
        } as usize;
        let mut all_ok = true;
        for (i, part) in buf.chunks_mut(chunk).enumerate() {
            let at = addr + (i * chunk) as u32;
            if self.link.read_mem32(at, part) {
                continue;
            }
            // Pin the fault down to the base's transfer unit.
            for (k, piece) in part.chunks_mut(BASE_PIECE as usize).enumerate() {
                let p = at + k as u32 * BASE_PIECE;
                if !self.link.read_mem32(p, piece) {
                    piece.fill(0);
                    all_ok = false;
                    if let Some(f) = failed.as_deref_mut() {
                        f.push((p, piece.len() as u32));
                    }
                }
            }
        }
        all_ok
    }

    /// Reads `len` bytes at `addr` into a new buffer, as
    /// [`read`](Self::read) with no fault list.
    ///
    /// # Errors
    ///
    /// "SWD read of 0x... failed" if any piece faulted.
    pub fn read_vec(&mut self, addr: u32, len: usize, chunk: u32) -> Result<Vec<u8>, String> {
        let mut buf = vec![0u8; len];
        if self.read(addr, &mut buf, chunk, None) {
            Ok(buf)
        } else {
            Err(format!("SWD read of 0x{addr:08X} (+{len}) failed"))
        }
    }

    /// Writes `data` at `addr` in 512-byte pieces.
    ///
    /// `addr` must be word aligned and the length a multiple of 4. Returns
    /// false on the first SWD fault; earlier pieces stay written.
    pub fn write(&mut self, addr: u32, data: &[u8]) -> bool {
        for (i, piece) in data.chunks(BASE_PIECE as usize).enumerate() {
            if !self.link.write_mem32(addr + i as u32 * BASE_PIECE, piece) {
                return false;
            }
        }
        true
    }

    /// Reads a core register of the halted core through DCRSR/DCRDR.
    ///
    /// `reg` is a DCRSR.REGSEL value: 0-12 R0-R12, 13 SP, 14 LR, 15 PC,
    /// 16 xPSR, 17 MSP, 18 PSP. `None` if the core is not halted (the
    /// transfer never completes) or a transfer failed.
    pub fn read_core_register(&mut self, reg: u32) -> Option<u32> {
        if !self.link.write_debug32(DCRSR, reg) {
            return None;
        }
        for _ in 0..100 {
            if self.link.read_debug32(DHCSR).is_some_and(|d| d & S_REGRDY != 0) {
                return self.link.read_debug32(DCRDR);
            }
            sleep(Duration::from_millis(1));
        }
        None
    }

    /// Writes a core register of the halted core through DCRSR/DCRDR.
    ///
    /// `reg` as for [`read_core_register`](Self::read_core_register).
    /// Returns false if the core is not halted or a transfer failed.
    pub fn write_core_register(&mut self, reg: u32, value: u32) -> bool {
        if !self.link.write_debug32(DCRDR, value) || !self.link.write_debug32(DCRSR, reg | REG_WNR) {
            return false;
        }
        for _ in 0..100 {
            if self.link.read_debug32(DHCSR).is_some_and(|d| d & S_REGRDY != 0) {
                return true;
            }
            sleep(Duration::from_millis(1));
        }
        false
    }

    /// Lets the halted core run, with interrupts masked by the debugger.
    ///
    /// Sets C_MASKINTS while still halted, as the architecture requires, then
    /// clears C_HALT with it held. The tag's own interrupts are therefore not
    /// taken while code downloaded by the host runs: one taken then would run
    /// tag firmware from internal flash. [`close`](Self::close) clears the
    /// bit again.
    ///
    /// Requires a halted core. Returns false on an SWD fault.
    pub fn run(&mut self) -> bool {
        self.link
            .write_debug32(DHCSR, DBGKEY | C_DEBUGEN | C_HALT | C_MASKINTS)
            && self.link.write_debug32(DHCSR, DBGKEY | C_DEBUGEN | C_MASKINTS)
    }

    /// Waits up to `timeout` for the core to halt.
    ///
    /// Returns whether S_HALT was seen, and DHCSR as last read (0 if no read
    /// succeeded).
    pub fn wait_halt(&mut self, timeout: Duration) -> (bool, u32) {
        let deadline = Instant::now() + timeout;
        let mut dhcsr = 0;
        loop {
            if let Some(d) = self.link.read_debug32(DHCSR) {
                dhcsr = d;
                if d & S_HALT != 0 {
                    return (true, dhcsr);
                }
            }
            if Instant::now() >= deadline {
                return (false, dhcsr);
            }
            sleep(Duration::from_millis(1));
        }
    }

    /// Requests a halt (keeping C_MASKINTS set, so no interrupt is taken on
    /// the way) and waits up to 100 ms for it. True when halted.
    pub fn halt(&mut self) -> bool {
        self.link
            .write_debug32(DHCSR, DBGKEY | C_DEBUGEN | C_HALT | C_MASKINTS)
            && self.wait_halt(Duration::from_millis(100)).0
    }
}

/// An IDCODE read that could be a real part: not all zeros or all ones,
/// which is what an absent or unpowered target reads as.
fn plausible_idcode(idcode: u32) -> bool {
    idcode != 0 && idcode != 0xFFFF_FFFF
}
