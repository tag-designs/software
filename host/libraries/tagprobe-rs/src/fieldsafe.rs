//! A probe-rs probe driver for our tag bases, using only the ST-LINK commands
//! every base in the field supports.
//!
//! The bases present as ST-LINK/V2s, but there is no record of which base
//! firmware is in the field, and some of it is old. probe-rs's own ST-LINK
//! driver relies on two things those bases get wrong: `READ_DAP_REG` (bases
//! flashed before 773db79a return stale data) and the 2-byte reply to a
//! 1-byte `READMEM_8BIT` (no base sends it). This driver never depends on
//! either. It speaks only the subset tagcore's [`LinkAdapt`] already uses on
//! every base:
//!
//! - USB open, mode query, DFU exit, SWD enter/exit, nRST drive, voltage;
//! - `READMEM_32BIT` / `WRITEMEM_32BIT` with the last-R/W-status check,
//!   in chunks of at most [`CHUNK`] bytes;
//! - `READDEBUGREG` for the chip ID, which tells L4 from U3.
//!
//! Everything else probe-rs asks for is answered here:
//!
//! - DAP register reads are answered from a table of the part's real values
//!   (DPIDR, the AP0 IDR, BASE and CSW, "no AP" past AP0, and 0 for any
//!   other register, CFG included); DAP writes
//!   are accepted and ignored, because memory never goes through DRW/TAR --
//!   it goes through the base's own memory commands, which set up the AP
//!   themselves.
//! - 8- and 16-bit memory accesses are done with 32-bit ones: reads take the
//!   bytes they need; writes read, patch and write back whole words. That is
//!   safe for SRAM and flash-algorithm buffers, which is what probe-rs uses
//!   them for, but would not be for a peripheral register with side effects.
//!
//! Reset and SWD entry happen in tagcore's order: nRST is asserted before
//! SWD is entered, because a tag asleep with its debug domain off answers SWD
//! only from reset.
//!
//! The layering is [`FieldSafeProbe`] (a probe-rs [`DebugProbe`]) ->
//! [`FieldSafeArm`] (its [`ArmDebugInterface`]) -> [`FieldSafeMemory`] (AP0's
//! [`ArmMemoryInterface`]). None of them is thread-safe; probe-rs drives a
//! session from one thread at a time. The C++ host tools have no counterpart:
//! they call STM32_Programmer_CLI for programming. See also
//! `host/libraries/tagprobe-rs/design/architecture.md`.

// The byte/word repacking below is as tested on the bench; keep it as is.
#![allow(clippy::chunks_exact_to_as_chunks)]

use std::collections::BTreeSet;
use std::fmt;
use std::sync::Arc;
use std::time::Duration;

use probe_rs::MemoryInterface;
use probe_rs::architecture::arm::ap::CSW;
use probe_rs::architecture::arm::dp::{DpAddress, DpRegisterAddress};
use probe_rs::architecture::arm::memory::ArmMemoryInterface;
use probe_rs::architecture::arm::sequences::ArmDebugSequence;
use probe_rs::architecture::arm::{
    ArmDebugInterface, ArmError, DapAccess, DapProbe, FullyQualifiedApAddress, SwdSequence,
    SwoAccess, SwoConfig,
};
use probe_rs::probe::{DebugProbe, DebugProbeError, Probe, ProbeError, WireProtocol};
use tagcore::{LinkAdapt, UsbDev};

/// Largest single memory transfer, in bytes, as tagcore's `TagMonitor` uses.
///
/// Every base firmware handles transfers of this size; larger reads and
/// writes are split into chunks of at most this many bytes.
pub const CHUNK: usize = 1024;

/// nRESET in the `swj_pins` bit map (the CMSIS-DAP `DAP_SWJ_Pins` layout
/// probe-rs uses).
const PIN_NRESET: u32 = 1 << 7;

/// A link failure, reported to probe-rs as a probe-specific error.
///
/// Holds the name of the step that failed ("open", "memory read", ...);
/// tagcore has already logged the underlying reason.
#[derive(Debug)]
pub struct LinkError(&'static str);

impl fmt::Display for LinkError {
    /// Formats as `tag base: <step> failed`.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "tag base: {} failed", self.0)
    }
}

impl std::error::Error for LinkError {}
impl ProbeError for LinkError {}

/// Wraps a failed step, named by `what`, as a probe-rs
/// [`DebugProbeError::ProbeSpecific`].
fn link_err(what: &'static str) -> DebugProbeError {
    DebugProbeError::ProbeSpecific(LinkError(what).into())
}

/// What the driver answers for the target's DAP registers.
///
/// probe-rs reads these while it identifies the debug port and access port;
/// the values are the ones the real part returns, so its discovery takes the
/// same path it would through a working probe.
#[derive(Debug, Clone, Copy)]
struct DapTable {
    /// DP `DPIDR` (DP register 0x0).
    dpidr: u32,
    /// AP0 `IDR` (AP register 0xFC); identifies the MEM-AP type.
    ap0_idr: u32,
    /// AP0 `BASE` (AP register 0xF8); the ROM table address, with its
    /// format/present bits.
    ap0_base: u32,
    /// AP0 `CSW` (AP register 0x00) as the part resets it.
    ap0_csw: u32,
}

/// STM32L4 (Cortex-M4): SW-DP v1 and an AHB-AP.
const L4_DAP: DapTable = DapTable {
    dpidr: 0x2BA0_1477,
    ap0_idr: 0x2477_0011,
    ap0_base: 0xE00F_F003,
    ap0_csw: 0x2300_0052,
};

/// STM32U3 (Cortex-M33): SW-DP v2 and an AHB5-AP. AP values read from an
/// STM32U375 through a base whose `READ_DAP_REG` works.
const U3_DAP: DapTable = DapTable {
    dpidr: 0x0BE1_2477,
    ap0_idr: 0x1477_0015,
    ap0_base: 0xE00F_E003,
    ap0_csw: 0x0380_0052,
};

/// Address of `DBGMCU_IDCODE` on the STM32L4.
const L4_DBGMCU_IDCODE: u32 = 0xE004_2000;
/// Address of `DBGMCU_IDCODE` on the STM32U3.
const U3_DBGMCU_IDCODE: u32 = 0xE004_4000;

/// Reports whether an `IDCODE` read looks real: a device ID (low 12 bits)
/// of all zeros or all ones is what a wrong address or a failed bus returns.
fn plausible(idcode: u32) -> bool {
    let dev = idcode & 0xfff;
    dev != 0 && dev != 0xfff
}

/// The probe: a tagcore [`LinkAdapt`] opened on one base.
///
/// Created by [`FieldSafeProbe::open`]. probe-rs then calls
/// [`DebugProbe::attach`] to enter SWD, after asserting reset for an attach
/// under reset. Dropping it (or [`DebugProbe::detach`]) leaves SWD mode and
/// closes the USB device.
pub struct FieldSafeProbe {
    /// The open base; every USB exchange goes through it.
    link: LinkAdapt,
    /// Whether SWD has been entered since the last detach (for `Debug` only).
    attached: bool,
}

impl fmt::Debug for FieldSafeProbe {
    /// Shows only `attached`; [`LinkAdapt`] has no `Debug` form.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("FieldSafeProbe")
            .field("attached", &self.attached)
            .finish()
    }
}

impl FieldSafeProbe {
    /// Opens the base `usbdev` selects, without touching nRST or SWD, and
    /// wraps it as a probe-rs [`Probe`].
    ///
    /// The tag is not reset until probe-rs attaches. Leaves DFU mode if the
    /// base is in it.
    ///
    /// # Errors
    ///
    /// A [`LinkError`] for "open" if no base matches `usbdev` or it cannot be
    /// claimed (for example because another process holds it).
    pub fn open(usbdev: &UsbDev) -> Result<Probe, DebugProbeError> {
        let mut link = LinkAdapt::new();
        if !link.open(usbdev) {
            return Err(link_err("open"));
        }
        Ok(Probe::from_specific_probe(Box::new(Self {
            link,
            attached: false,
        })))
    }
}

impl DebugProbe for FieldSafeProbe {
    /// Returns the name probe-rs prints for this probe.
    fn get_name(&self) -> &str {
        "Tag base (ST-LINK/V2 field subset)"
    }

    /// Reports a nominal 1000 kHz; the base sets its own SWD clock.
    fn speed_khz(&self) -> u32 {
        // The base runs SWD at its own fixed rate.
        1000
    }

    /// Accepts any speed and reports it back; nothing is sent to the base.
    fn set_speed(&mut self, speed_khz: u32) -> Result<u32, DebugProbeError> {
        Ok(speed_khz)
    }

    /// Enters SWD mode. Does not touch nRST: for an attach under reset,
    /// probe-rs has already asserted it through
    /// [`target_reset_assert`](DebugProbe::target_reset_assert).
    fn attach(&mut self) -> Result<(), DebugProbeError> {
        if !self.link.enter_swd() {
            return Err(link_err("SWD entry"));
        }
        self.attached = true;
        Ok(())
    }

    /// Leaves SWD mode and closes the base; always succeeds.
    fn detach(&mut self) -> Result<(), probe_rs::Error> {
        self.link.detach();
        self.attached = false;
        Ok(())
    }

    /// Pulses nRST: asserts it, waits 10 ms and releases it. The tag reboots.
    fn target_reset(&mut self) -> Result<(), DebugProbeError> {
        self.target_reset_assert()?;
        std::thread::sleep(Duration::from_millis(10));
        self.target_reset_deassert()
    }

    /// Drives nRST low, holding the tag in reset and ending whatever it was
    /// doing.
    fn target_reset_assert(&mut self) -> Result<(), DebugProbeError> {
        // LinkAdapt::assert_reset(high): false drives nRST low.
        if self.link.assert_reset(false) {
            Ok(())
        } else {
            Err(link_err("nRST assert"))
        }
    }

    /// Releases nRST, letting the tag boot (or stop at the reset vector if
    /// vector catch is armed).
    fn target_reset_deassert(&mut self) -> Result<(), DebugProbeError> {
        if self.link.assert_reset(true) {
            Ok(())
        } else {
            Err(link_err("nRST release"))
        }
    }

    /// Accepts SWD only; the bases have no JTAG.
    fn select_protocol(&mut self, protocol: WireProtocol) -> Result<(), DebugProbeError> {
        match protocol {
            WireProtocol::Swd => Ok(()),
            other => Err(DebugProbeError::UnsupportedProtocol(other)),
        }
    }

    /// Always SWD.
    fn active_protocol(&self) -> Option<WireProtocol> {
        Some(WireProtocol::Swd)
    }

    /// Always true: the tags are Arm Cortex-M parts.
    fn has_arm_interface(&self) -> bool {
        true
    }

    /// Wraps the probe in a [`FieldSafeArm`]; never fails.
    ///
    /// The debug sequence is ignored here: the sequence's own steps run
    /// through the returned interface, and the DAP power-up handshake it
    /// performs is answered from the `DapTable`.
    fn try_get_arm_debug_interface<'probe>(
        self: Box<Self>,
        _sequence: Arc<dyn ArmDebugSequence>,
    ) -> Result<Box<dyn ArmDebugInterface + 'probe>, (Box<dyn DebugProbe>, ArmError)> {
        Ok(Box::new(FieldSafeArm {
            probe: self,
            table: None,
        }))
    }

    /// Returns the probe itself as a boxed [`DebugProbe`].
    fn into_probe(self: Box<Self>) -> Box<dyn DebugProbe> {
        self
    }

    /// Returns the target supply voltage the base measures, in volts; `None`
    /// if the voltage command fails.
    fn get_target_voltage(&mut self) -> Result<Option<f32>, DebugProbeError> {
        Ok(self.link.voltage())
    }
}

/// The ARM debug interface over a [`FieldSafeProbe`].
///
/// Serves DP/AP register traffic from a `DapTable` and memory through the
/// base's 32-bit memory commands (via [`FieldSafeMemory`]). Only the default
/// debug port and AP0 exist.
#[derive(Debug)]
pub struct FieldSafeArm {
    /// The probe, owned while the interface is in use and handed back by
    /// [`ArmDebugInterface::close`].
    probe: Box<FieldSafeProbe>,
    /// The DAP answers for the part, chosen on first use from its chip ID.
    table: Option<DapTable>,
}

impl FieldSafeArm {
    /// The DAP table for the connected part: L4 or U3 by DBGMCU IDCODE, read
    /// with the debug-register command every base supports.
    ///
    /// Cached after the first success, so the part is identified once per
    /// session.
    ///
    /// # Errors
    ///
    /// [`ArmError::Probe`] with "chip identification" if neither IDCODE
    /// address reads plausibly (no tag, an unpowered tag, or another part).
    fn table(&mut self) -> Result<DapTable, ArmError> {
        if let Some(t) = self.table {
            return Ok(t);
        }
        let link = &mut self.probe.link;
        let table = if link.read_debug32(L4_DBGMCU_IDCODE).is_some_and(plausible) {
            L4_DAP
        } else if link.read_debug32(U3_DBGMCU_IDCODE).is_some_and(plausible) {
            U3_DAP
        } else {
            return Err(ArmError::Probe(link_err("chip identification")));
        };
        tracing::debug!("tag base: DAP table {table:x?}");
        self.table = Some(table);
        Ok(table)
    }

    /// Reads `data.len()` words at `address` with `READMEM_32BIT`, in chunks
    /// of at most [`CHUNK`] bytes.
    ///
    /// `address` must be word-aligned, as probe-rs guarantees for 32-bit
    /// accesses. The core is not halted for the read.
    ///
    /// # Errors
    ///
    /// [`ArmError::Probe`] ("memory read") if any chunk fails, e.g. on a bus
    /// fault; `data` is then left unchanged.
    fn read_words(&mut self, address: u32, data: &mut [u32]) -> Result<(), ArmError> {
        let mut bytes = vec![0u8; data.len() * 4];
        for (i, chunk) in bytes.chunks_mut(CHUNK).enumerate() {
            let at = address + (i * CHUNK) as u32;
            if !self.probe.link.read_mem32(at, chunk) {
                return Err(ArmError::Probe(link_err("memory read")));
            }
        }
        for (word, b) in data.iter_mut().zip(bytes.chunks_exact(4)) {
            *word = u32::from_le_bytes(b.try_into().unwrap());
        }
        Ok(())
    }

    /// Writes `data` at `address` with `WRITEMEM_32BIT`, in chunks of at most
    /// [`CHUNK`] bytes.
    ///
    /// `address` must be word-aligned.
    ///
    /// # Errors
    ///
    /// [`ArmError::Probe`] ("memory write") if any chunk fails; earlier
    /// chunks have then already been written.
    fn write_words(&mut self, address: u32, data: &[u32]) -> Result<(), ArmError> {
        let bytes: Vec<u8> = data.iter().flat_map(|w| w.to_le_bytes()).collect();
        for (i, chunk) in bytes.chunks(CHUNK).enumerate() {
            let at = address + (i * CHUNK) as u32;
            if !self.probe.link.write_mem32(at, chunk) {
                return Err(ArmError::Probe(link_err("memory write")));
            }
        }
        Ok(())
    }

    /// Reads `data.len()` bytes at any alignment with whole-word reads.
    ///
    /// Reads the enclosing aligned words and copies out the bytes asked for,
    /// so up to 3 bytes either side are also read from the target.
    ///
    /// # Errors
    ///
    /// As [`read_words`](Self::read_words).
    fn read_bytes(&mut self, address: u32, data: &mut [u8]) -> Result<(), ArmError> {
        if data.is_empty() {
            return Ok(());
        }
        let start = address & !3;
        let end = (address as u64 + data.len() as u64 + 3) & !3;
        let mut words = vec![0u32; ((end - start as u64) / 4) as usize];
        self.read_words(start, &mut words)?;
        let bytes: Vec<u8> = words.iter().flat_map(|w| w.to_le_bytes()).collect();
        let offset = (address - start) as usize;
        data.copy_from_slice(&bytes[offset..offset + data.len()]);
        Ok(())
    }

    /// Writes bytes at any alignment by read-modify-write of whole words.
    ///
    /// The bytes either side of `data` within the edge words are written back
    /// with the values just read. That is not atomic: anything the target
    /// changes in those bytes between the read and the write is lost, so this
    /// is only safe for memory the core is not using (probe-rs uses it for
    /// halted-core SRAM and flash-algorithm buffers), never for peripheral
    /// registers.
    ///
    /// # Errors
    ///
    /// As [`read_words`](Self::read_words) and
    /// [`write_words`](Self::write_words).
    fn write_bytes(&mut self, address: u32, data: &[u8]) -> Result<(), ArmError> {
        if data.is_empty() {
            return Ok(());
        }
        let start = address & !3;
        let end = (address as u64 + data.len() as u64 + 3) & !3;
        let mut words = vec![0u32; ((end - start as u64) / 4) as usize];
        self.read_words(start, &mut words)?;
        let mut bytes: Vec<u8> = words.iter().flat_map(|w| w.to_le_bytes()).collect();
        let offset = (address - start) as usize;
        bytes[offset..offset + data.len()].copy_from_slice(data);
        let words: Vec<u32> = bytes
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
            .collect();
        self.write_words(start, &words)
    }
}

impl DapAccess for FieldSafeArm {
    /// Answers DP register reads from the `DapTable` without touching the
    /// wire.
    ///
    /// `DPIDR` gets the part's value; `CTRL/STAT` (bank 0) reports debug and
    /// system power-up requested and acknowledged, which ends probe-rs's
    /// power-up handshake; everything else reads 0. The bases' own
    /// `READ_DAP_REG` cannot be used: old firmware returns stale data for it.
    fn read_raw_dp_register(
        &mut self,
        _dp: DpAddress,
        address: DpRegisterAddress,
    ) -> Result<u32, ArmError> {
        let t = self.table()?;
        Ok(match (address.address, address.bank.unwrap_or(0)) {
            (0x0, _) => t.dpidr,
            // CTRL/STAT: debug and system power-up requested and acknowledged.
            (0x4, 0) => 0xF000_0000,
            _ => 0,
        })
    }

    /// Accepts and ignores DP register writes; the base powers up and selects
    /// the DAP itself on SWD entry.
    fn write_raw_dp_register(
        &mut self,
        _dp: DpAddress,
        _address: DpRegisterAddress,
        _value: u32,
    ) -> Result<(), ArmError> {
        Ok(())
    }

    /// Answers AP register reads from the `DapTable` without touching the
    /// wire.
    ///
    /// AP0 reports its `CSW`, `BASE` and `IDR`; other registers read 0. Any AP
    /// other than AP0 reads 0 throughout, which probe-rs takes as "no AP".
    fn read_raw_ap_register(
        &mut self,
        ap: &FullyQualifiedApAddress,
        address: u64,
    ) -> Result<u32, ArmError> {
        if ap.ap_v1()? != 0 {
            // No AP past AP0: an IDR of 0 ends probe-rs's AP scan.
            return Ok(0);
        }
        let t = self.table()?;
        Ok(match address & 0xFF {
            0x00 => t.ap0_csw,
            0xF8 => t.ap0_base,
            0xFC => t.ap0_idr,
            _ => 0,
        })
    }

    /// Accepts and ignores AP register writes: memory never goes through
    /// `TAR`/`DRW`, and the base's memory commands set `CSW` themselves.
    fn write_raw_ap_register(
        &mut self,
        _ap: &FullyQualifiedApAddress,
        _address: u64,
        _value: u32,
    ) -> Result<(), ArmError> {
        Ok(())
    }

    /// Returns `None`: there is no raw DAP access to offer.
    fn try_dap_probe(&self) -> Option<&dyn DapProbe> {
        None
    }

    /// Returns `None`: there is no raw DAP access to offer.
    fn try_dap_probe_mut(&mut self) -> Option<&mut dyn DapProbe> {
        None
    }
}

impl SwdSequence for FieldSafeArm {
    /// Refuses raw SWJ bit sequences (line resets, JTAG-to-SWD switches); the
    /// base performs those itself on SWD entry.
    fn swj_sequence(&mut self, _bit_len: u8, _bits: u64) -> Result<(), DebugProbeError> {
        Err(DebugProbeError::CommandNotSupportedByProbe {
            command_name: "swj_sequence",
        })
    }

    /// Drives nRST when that is the only pin selected; refuses any other pin.
    ///
    /// probe-rs's reset sequences use this to assert and release reset.
    /// Returns `pin_out` as the pin state read back; `pin_wait` is ignored.
    fn swj_pins(
        &mut self,
        pin_out: u32,
        pin_select: u32,
        _pin_wait: u32,
    ) -> Result<u32, DebugProbeError> {
        if pin_select != PIN_NRESET {
            return Err(DebugProbeError::CommandNotSupportedByProbe {
                command_name: "swj_pins",
            });
        }
        if pin_out & PIN_NRESET == 0 {
            self.probe.target_reset_assert()?;
        } else {
            self.probe.target_reset_deassert()?;
        }
        Ok(pin_out)
    }
}

impl SwoAccess for FieldSafeArm {
    /// Refuses: the bases do not capture SWO.
    fn enable_swo(&mut self, _config: &SwoConfig) -> Result<(), ArmError> {
        Err(ArmError::NotImplemented("SWO on a tag base"))
    }

    /// Does nothing; SWO is never enabled.
    fn disable_swo(&mut self) -> Result<(), ArmError> {
        Ok(())
    }

    /// Refuses: the bases do not capture SWO.
    fn read_swo_timeout(&mut self, _timeout: Duration) -> Result<Vec<u8>, ArmError> {
        Err(ArmError::NotImplemented("SWO on a tag base"))
    }
}

impl ArmDebugInterface for FieldSafeArm {
    /// Does nothing: the DAP state is the table's, and the base keeps the
    /// link up itself.
    fn reinitialize(&mut self) -> Result<(), ArmError> {
        Ok(())
    }

    /// Returns AP0 on the default DP, the only access port these parts use.
    fn access_ports(
        &mut self,
        _dp: DpAddress,
    ) -> Result<BTreeSet<FullyQualifiedApAddress>, ArmError> {
        Ok(BTreeSet::from([
            FullyQualifiedApAddress::v1_with_default_dp(0),
        ]))
    }

    /// Hands the [`FieldSafeProbe`] back as an unattached probe-rs [`Probe`].
    ///
    /// The base stays in SWD mode; the probe is only ever dropped afterwards,
    /// which detaches it.
    fn close(self: Box<Self>) -> Probe {
        // from_attached_probe() is crate-private; the spike never reuses a
        // closed probe, so an unattached Probe is enough.
        Probe::from_specific_probe(self.probe)
    }

    /// Accepts only the default DP; multidrop SWD is not supported.
    fn select_debug_port(&mut self, dp: DpAddress) -> Result<(), ArmError> {
        if dp == DpAddress::Default {
            Ok(())
        } else {
            Err(ArmError::NotImplemented("multidrop on a tag base"))
        }
    }

    /// Always the default DP.
    fn current_debug_port(&self) -> Option<DpAddress> {
        Some(DpAddress::Default)
    }

    /// Returns a [`FieldSafeMemory`] for AP0; any other AP is refused.
    fn memory_interface(
        &mut self,
        access_port: &FullyQualifiedApAddress,
    ) -> Result<Box<dyn ArmMemoryInterface + '_>, ArmError> {
        if access_port.ap_v1()? != 0 {
            return Err(ArmError::NotImplemented("APs other than AP0 on a tag base"));
        }
        Ok(Box::new(FieldSafeMemory {
            arm: self,
            ap: access_port.clone(),
        }))
    }
}

/// AP0 memory through the base's 32-bit memory commands.
///
/// Every access width is carried by 32-bit transfers: 64-bit as word pairs,
/// 8- and 16-bit through [`FieldSafeArm`]'s byte helpers (read-modify-write
/// for writes). Addresses above 4 GB are refused with
/// [`ArmError::OutOfBounds`].
#[derive(Debug)]
pub struct FieldSafeMemory<'a> {
    /// The interface whose link carries the transfers.
    arm: &'a mut FieldSafeArm,
    /// The access port this interface was opened for (always AP0).
    ap: FullyQualifiedApAddress,
}

/// Narrows a probe-rs 64-bit address to the 32-bit address the base takes.
///
/// # Errors
///
/// [`ArmError::OutOfBounds`] if `address` does not fit in 32 bits.
fn addr32(address: u64) -> Result<u32, ArmError> {
    u32::try_from(address).map_err(|_| ArmError::OutOfBounds)
}

impl SwdSequence for FieldSafeMemory<'_> {
    /// Forwards to [`FieldSafeArm`], which refuses it.
    fn swj_sequence(&mut self, bit_len: u8, bits: u64) -> Result<(), DebugProbeError> {
        self.arm.swj_sequence(bit_len, bits)
    }

    /// Forwards to [`FieldSafeArm`]: drives nRST only.
    fn swj_pins(
        &mut self,
        pin_out: u32,
        pin_select: u32,
        pin_wait: u32,
    ) -> Result<u32, DebugProbeError> {
        self.arm.swj_pins(pin_out, pin_select, pin_wait)
    }
}

impl MemoryInterface<ArmError> for FieldSafeMemory<'_> {
    /// Returns false: 64-bit accesses are split into word pairs.
    fn supports_native_64bit_access(&mut self) -> bool {
        false
    }

    /// Reads double words as pairs of little-endian words.
    fn read_64(&mut self, address: u64, data: &mut [u64]) -> Result<(), ArmError> {
        let mut words = vec![0u32; data.len() * 2];
        self.arm.read_words(addr32(address)?, &mut words)?;
        for (d, w) in data.iter_mut().zip(words.chunks_exact(2)) {
            *d = u64::from(w[0]) | (u64::from(w[1]) << 32);
        }
        Ok(())
    }

    /// Reads words directly with `READMEM_32BIT`.
    fn read_32(&mut self, address: u64, data: &mut [u32]) -> Result<(), ArmError> {
        self.arm.read_words(addr32(address)?, data)
    }

    /// Reads half-words through whole-word reads.
    fn read_16(&mut self, address: u64, data: &mut [u16]) -> Result<(), ArmError> {
        let mut bytes = vec![0u8; data.len() * 2];
        self.arm.read_bytes(addr32(address)?, &mut bytes)?;
        for (d, b) in data.iter_mut().zip(bytes.chunks_exact(2)) {
            *d = u16::from_le_bytes([b[0], b[1]]);
        }
        Ok(())
    }

    /// Reads bytes through whole-word reads, never `READMEM_8BIT`.
    fn read_8(&mut self, address: u64, data: &mut [u8]) -> Result<(), ArmError> {
        self.arm.read_bytes(addr32(address)?, data)
    }

    /// Writes double words as pairs of little-endian words.
    fn write_64(&mut self, address: u64, data: &[u64]) -> Result<(), ArmError> {
        let words: Vec<u32> = data
            .iter()
            .flat_map(|d| [*d as u32, (*d >> 32) as u32])
            .collect();
        self.arm.write_words(addr32(address)?, &words)
    }

    /// Writes words directly with `WRITEMEM_32BIT`.
    fn write_32(&mut self, address: u64, data: &[u32]) -> Result<(), ArmError> {
        self.arm.write_words(addr32(address)?, data)
    }

    /// Writes half-words by read-modify-write of whole words; see
    /// [`FieldSafeArm`]'s byte-write caveats.
    fn write_16(&mut self, address: u64, data: &[u16]) -> Result<(), ArmError> {
        let bytes: Vec<u8> = data.iter().flat_map(|h| h.to_le_bytes()).collect();
        self.arm.write_bytes(addr32(address)?, &bytes)
    }

    /// Writes bytes by read-modify-write of whole words; see
    /// [`FieldSafeArm`]'s byte-write caveats.
    fn write_8(&mut self, address: u64, data: &[u8]) -> Result<(), ArmError> {
        self.arm.write_bytes(addr32(address)?, data)
    }

    /// Returns true: byte transfers are emulated, so probe-rs may use them.
    fn supports_8bit_transfers(&self) -> Result<bool, ArmError> {
        Ok(true)
    }

    /// Does nothing: every transfer completes before its call returns.
    fn flush(&mut self) -> Result<(), ArmError> {
        Ok(())
    }
}

impl ArmMemoryInterface for FieldSafeMemory<'_> {
    /// Returns the ROM table address from the `DapTable`'s AP0 `BASE`,
    /// with its format/present bits masked off.
    fn base_address(&mut self) -> Result<u64, ArmError> {
        Ok(u64::from(self.arm.table()?.ap0_base & !0xFFF))
    }

    /// Returns the access port this interface was opened for (AP0).
    fn fully_qualified_address(&self) -> FullyQualifiedApAddress {
        self.ap.clone()
    }

    /// Returns the [`FieldSafeArm`] this memory interface belongs to.
    fn get_arm_debug_interface(&mut self) -> Result<&mut dyn ArmDebugInterface, DebugProbeError> {
        Ok(self.arm)
    }

    /// Returns AP0's `CSW` from the `DapTable`, as the part resets it.
    fn generic_status(&mut self) -> Result<CSW, ArmError> {
        Ok(CSW::from_raw(self.arm.table()?.ap0_csw))
    }
}
