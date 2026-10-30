//! Programming tags through the tag bases with probe-rs.
//!
//! The replacement for qtprogram's STM32_Programmer_CLI calls: list the
//! bases, ask each who it is, check the connection, program an image with an
//! erase plan, and read flash back. Every operation picks its probe driver
//! from the base's identity:
//!
//! - a base that answers the identify command (`F6 99`, see
//!   `embedded/bases/README.md`) has the ST-LINK fixes, so probe-rs's own
//!   ST-LINK driver is used ([`Driver::Stock`]);
//! - a base that does not -- every base in the field -- is driven through
//!   [`FieldSafeProbe`], which uses only the commands all bases support
//!   ([`Driver::FieldSafe`]).
//!
//! Attaches here go through reset, so they end whatever the tag was doing,
//! like every tool in this tree. Nothing here may run while a tagcore
//! monitor session holds the same base: detach first.
//!
//! The evaluation that led here is
//! `docs/investigations/2026-10-probe-rs-evaluation.md`; the crate's design is
//! in `host/libraries/tagprobe-rs/design/architecture.md`.
//!
//! # Hardware side effects
//!
//! - [`list`] and [`identify`] never touch nRST or SWD, so the tag keeps
//!   running.
//! - [`connect_check`], [`program`], [`erase_all`] and [`read_memory`] all
//!   hold the tag in reset to attach. A running log is ended, and the tag
//!   reboots its firmware afterwards. [`program`] also rewrites internal
//!   flash, and [`erase_all`] empties it.
//!
//! # Threading
//!
//! Every call opens the base, uses it and closes it again; nothing is held
//! between calls, so calls may come from any thread. Two calls on the *same*
//! base at once are not serialised here and would interleave their USB
//! exchanges: the caller owns that exclusion. The Stock driver is opened
//! through [`StLinkFactory`] only, never probe-rs's `Lister`; see
//! [`open_probe`] for why that matters on macOS.
//!
//! # Examples
//!
//! The connect check and programming sequence:
//!
//! ```no_run
//! use std::path::Path;
//! use tagprobe::{Driver, Erase, Progress};
//!
//! let base = tagprobe::list().into_iter().next().expect("no base on USB");
//! let driver = Driver::for_identity(tagprobe::identify(&base).as_deref());
//! let found = tagprobe::connect_check(&base)?;
//! let chip = tagprobe::chip_for(found.device_id, Some(0x40000))?;
//! tagprobe::program(&base, driver, chip, Path::new("tag.elf"), Erase::ImagePages, |p| {
//!     if let Progress::Step(step) = p {
//!         println!("{step}");
//!     }
//! })?;
//! # Ok::<(), String>(())
//! ```

pub mod fieldsafe;

use std::path::Path;
use std::sync::Arc;
use std::time::Duration;

use probe_rs::architecture::arm::sequences::ArmDebugSequence;
use probe_rs::config::{DebugSequence, Registry, TargetSelector};
use probe_rs::flashing::{
    self, DownloadOptions, ElfLoader, FlashProgress, ProgressEvent, ProgressOperation,
};
use probe_rs::probe::Probe;
use probe_rs::probe::stlink::StLinkFactory;
use probe_rs::probe::{DebugProbeSelector, ProbeFactory};
use probe_rs::{MemoryInterface, Permissions, Session};
use tagcore::{LinkAdapt, UsbDev};

pub use fieldsafe::FieldSafeProbe;

/// The probe-rs release these tools are built and tested against.
///
/// For display (an "about" line or a log header). It is not read from
/// `Cargo.toml`, so it must be updated by hand with the dependency. The
/// field-safe driver implements probe-rs's probe traits, which change between
/// minor releases, so a probe-rs upgrade needs the bench checks in the
/// evaluation document repeated.
pub const PROBE_RS_VERSION: &str = "0.32.0";

/// STM32L43x/L44x DBGMCU device ID.
///
/// The low 12 bits of `DBGMCU_IDCODE`. The L432 tags (BitTag, PresTag,
/// CompassTag, UIUCTag) report it.
pub const DEV_L432: u16 = 0x435;
/// STM32U37x/U38x DBGMCU device ID.
///
/// The low 12 bits of `DBGMCU_IDCODE`. The IMUTag boards (STM32U375) report
/// it.
pub const DEV_U375: u16 = 0x454;

/// Address of `DBGMCU_IDCODE` on the STM32L4.
const L4_DBGMCU_IDCODE: u32 = 0xE004_2000;
/// Address of `DBGMCU_IDCODE` on the STM32U3.
const U3_DBGMCU_IDCODE: u32 = 0xE004_4000;
/// Address of the STM32L4 flash size data register, `FLASHSIZE_BASE`: the
/// part's internal flash in KB, in the low 16 bits.
const L4_FLASHSIZE: u32 = 0x1FFF_75E0;
/// Address of the STM32L4 flash status register, `FLASH_SR`.
const L4_FLASH_SR: u64 = 0x4002_2010;
/// `FLASH_SR.PEMPTY` (bit 17) on the STM32L4.
///
/// Records whether user flash read as empty at the last reset. While it is
/// set, the part boots its system ROM bootloader instead of the image, so it
/// must be clear after programming an erased part; see [`clear_l4_pempty`].
const L4_FLASH_SR_PEMPTY: u32 = 1 << 17;
/// Start of internal flash, where every tag image's vector table sits.
const FLASH_BASE: u64 = 0x0800_0000;

/// Which probe-rs driver drives a base.
///
/// Both drivers yield the same probe-rs [`Probe`], so everything above
/// [`open_probe`] is driver-independent. Choose with [`Driver::for_identity`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Driver {
    /// probe-rs's own ST-LINK driver: the base answers the identify command,
    /// so it has the ST-LINK fixes.
    Stock,
    /// [`FieldSafeProbe`]: the base predates the identify command.
    FieldSafe,
}

impl Driver {
    /// Returns the driver for a base with this identity (from [`identify`]).
    ///
    /// Any identity string selects [`Driver::Stock`]: the identify command
    /// shipped in the same base firmware as the ST-LINK fixes the stock driver
    /// needs. `None` -- a base that predates the command, or a genuine
    /// ST-LINK -- selects [`Driver::FieldSafe`], which works on every base.
    pub fn for_identity(identity: Option<&str>) -> Self {
        if identity.is_some() {
            Driver::Stock
        } else {
            Driver::FieldSafe
        }
    }

    /// Returns a short human-readable description, e.g. for a status line.
    pub fn name(self) -> &'static str {
        match self {
            Driver::Stock => "probe-rs ST-LINK driver",
            Driver::FieldSafe => "field-safe driver",
        }
    }
}

/// Lists the ST-LINK-compatible probes on USB (bases and genuine ST-LINKs
/// alike).
///
/// Uses tagcore's [`LinkAdapt::available`], the enumeration the monitor tools
/// use, so the [`UsbDev`] values match theirs. No probe is opened for debug
/// and no tag is reset.
pub fn list() -> Vec<UsbDev> {
    LinkAdapt::new().available()
}

/// Asks a base who it is with the bases' own `F6 99` identify command.
///
/// Returns the identity string (see [`LinkAdapt::base_identity`] for its
/// format), or `None` for a base that predates the command, a genuine
/// ST-LINK, or a base that cannot be opened (for example because another
/// process holds it). Opens the base without touching nRST or SWD, so the tag
/// is not reset.
///
/// Blocks for the identify reply's timeout (a few hundred milliseconds) when
/// the base does not answer, because some old base firmware never replies.
pub fn identify(usbdev: &UsbDev) -> Option<String> {
    let mut link = LinkAdapt::new();
    if !link.open(usbdev) {
        return None;
    }
    let identity = link.base_identity();
    link.detach();
    identity
}

/// What a connect check found (`STM32_Programmer_CLI -c port=SWD mode=UR`).
///
/// Returned by [`connect_check`].
#[derive(Debug, Clone, PartialEq)]
pub struct Connect {
    /// DBGMCU device ID, the low 12 bits of `DBGMCU_IDCODE`, e.g.
    /// [`DEV_L432`]. Any plausible ID is reported, including parts this crate
    /// has no target for; [`chip_for`] is where those are refused.
    pub device_id: u16,
    /// The part family's name, as STM32CubeProgrammer prints it; see
    /// [`device_name`].
    pub device_name: &'static str,
    /// Target supply voltage measured by the base, in volts; `None` if the
    /// base's voltage command failed.
    pub voltage: Option<f32>,
    /// The part's internal flash in bytes, from its flash size register, for
    /// [`chip_for`]; `None` when it was not read.
    ///
    /// Read on the STM32L4 only, where it tells an L432KB (128 KB, the
    /// UIUCTag) from an L432KC (256 KB) running the same image. It is `None`
    /// on the STM32U375, whose tags are all 1 MB parts.
    pub flash_bytes: Option<u32>,
}

/// Returns the family name for a DBGMCU device ID, as STM32CubeProgrammer
/// prints it, or `"unknown STM32"` for an ID this crate does not know.
pub fn device_name(device_id: u16) -> &'static str {
    match device_id {
        DEV_L432 => "STM32L43xxx/STM32L44xxx",
        DEV_U375 => "STM32U37xx/STM32U38xx",
        _ => "unknown STM32",
    }
}

/// Returns the probe-rs target name for a device, from its ID and the flash
/// size its image was linked for (`__flash0_end__ - __flash0_base__`).
///
/// The device ID does not give the flash size, and probe-rs's flash layout and
/// chip erase depend on it (a KC target on a 128 KB KB would address flash
/// that is not there), so the caller supplies the size -- from the tag's
/// `TagInfo.intflashsz` or the image's linker symbols -- to choose the
/// variant:
///
/// | Device | `flash_bytes` | Target |
/// | --- | --- | --- |
/// | [`DEV_L432`] | `None` or 256 KB | `STM32L432KC` |
/// | [`DEV_L432`] | 128 KB | `STM32L432KB` |
/// | [`DEV_U375`] | `None` or 1 MB | `STM32U375RG` |
/// | [`DEV_U375`] | 512 KB | `STM32U375RE` |
///
/// `None` (an image without the symbols, or no image at all) takes the larger
/// part.
///
/// # Errors
///
/// A message naming the device ID, and the size if one was given, for any
/// other combination; nothing is guessed.
pub fn chip_for(device_id: u16, flash_bytes: Option<u32>) -> Result<&'static str, String> {
    match (device_id, flash_bytes) {
        (DEV_L432, None | Some(0x40000)) => Ok("STM32L432KC"),
        (DEV_L432, Some(0x20000)) => Ok("STM32L432KB"),
        (DEV_U375, None | Some(0x100000)) => Ok("STM32U375RG"),
        (DEV_U375, Some(0x80000)) => Ok("STM32U375RE"),
        (id, Some(size)) => Err(format!(
            "no probe-rs target for device 0x{id:03x} with {} KB of flash",
            size / 1024
        )),
        (id, None) => Err(format!("no probe-rs target for device 0x{id:03x}")),
    }
}

/// Holds the tag in reset, reads its device ID and the supply voltage, and
/// releases it: the connect check. The tag reboots afterwards.
///
/// The counterpart of qtprogram's `STM32_Programmer_CLI -c port=SWD mode=UR`
/// call. It uses tagcore's [`LinkAdapt`] directly rather than a probe-rs
/// session, so it is the same on every base generation and needs no target
/// name: the ID is read from the L4 `DBGMCU_IDCODE` address and then the U3
/// one, and the first plausible value (low 12 bits neither all zeros nor all
/// ones) wins.
///
/// Whatever the tag was doing is ended by the reset, and releasing nRST at the
/// end boots its firmware from the start.
///
/// # Errors
///
/// - `"Cannot open the base"` if the base cannot be opened or attached;
/// - `"No STM32 target found"` if neither ID address reads plausibly (no tag
///   on the cradle, an unpowered tag, or an unrecognised part).
pub fn connect_check(usbdev: &UsbDev) -> Result<Connect, String> {
    let mut link = LinkAdapt::new();
    if !link.attach_with_reset(true, usbdev) {
        return Err("Cannot open the base".into());
    }
    let voltage = link.voltage();
    let plausible = |id: u32| (id & 0xfff) != 0 && (id & 0xfff) != 0xfff;
    let id = [L4_DBGMCU_IDCODE, U3_DBGMCU_IDCODE]
        .iter()
        .find_map(|a| link.read_debug32(*a).filter(|v| plausible(*v)));
    let flash_bytes = id
        .filter(|idcode| (idcode & 0xfff) as u16 == DEV_L432)
        .and_then(|_| link.read_debug32(L4_FLASHSIZE))
        .map(|kb| (kb & 0xffff) * 1024)
        .filter(|&bytes| bytes > 0);
    link.assert_reset(true);
    link.detach();
    match id {
        Some(idcode) => {
            let device_id = (idcode & 0xfff) as u16;
            Ok(Connect {
                device_id,
                device_name: device_name(device_id),
                voltage,
                flash_bytes,
            })
        }
        None => Err("No STM32 target found".into()),
    }
}

/// The attach/detach sequence used for every session: the ARM default, which
/// writes nothing to DBGMCU and on detach clears DHCSR.C_DEBUGEN and DEMCR.
///
/// probe-rs's built-in STM32 sequences (`Stm32Armv7` for the L4,
/// `Stm32Armv8` for the U3) set DBGMCU_CR's debug-in-Stop/Standby bits on
/// attach and never clear C_DEBUGEN. The default sequence leaves the tag as a
/// normal boot would, so nothing about its low-power state depends on having
/// been programmed. (Joulescope measurements after each kind of attach found
/// no difference in resting current; see the evaluation document. Note that
/// every base sets C_DEBUGEN on SWD entry regardless.)
#[derive(Debug)]
pub struct TagSequence;

impl ArmDebugSequence for TagSequence {}

/// Opens `usbdev` with `driver`, as a probe-rs [`Probe`].
///
/// Nothing is attached yet: the tag is not reset until the caller attaches.
///
/// For [`Driver::Stock`] the base is found among the ST-LINKs by USB vendor
/// and product ID and then by serial number. If the serial does not match (or
/// `usbdev` has none) and there is exactly one candidate, that one is used.
///
/// The Stock path enumerates and opens through [`StLinkFactory`] **only, never
/// probe-rs's `Lister`**. `Lister` also enumerates CMSIS-DAP probes over
/// hidapi, and on macOS hidapi binds its `IOHIDManager` to the first thread
/// that used it; a second programming run from another worker thread (as
/// tagprogram-rs does) then aborted the whole process. Going back to `Lister`
/// for convenience brings that crash back (fixed in commit 36c06ce2).
///
/// # Errors
///
/// The probe driver's message if the base cannot be opened, or `"Cannot tell
/// this base from the other ST-LINKs"` when several ST-LINKs are connected
/// and none matches `usbdev`'s serial number.
pub fn open_probe(usbdev: &UsbDev, driver: Driver) -> Result<Probe, String> {
    match driver {
        Driver::FieldSafe => FieldSafeProbe::open(usbdev).map_err(|e| e.to_string()),
        Driver::Stock => {
            // Only the ST-LINK driver, never Lister: listing every kind of
            // probe enumerates CMSIS-DAP over HID, and on macOS hidapi ties
            // its IOHIDManager to the first thread that used it, so a second
            // program from another worker thread aborted the process.
            let probes: Vec<_> = StLinkFactory
                .list_probes()
                .into_iter()
                .map(|item| item.info)
                .filter(|p| p.vendor_id == usbdev.vid && p.product_id == usbdev.pid)
                .collect();
            let info = match probes.iter().find(|p| {
                !usbdev.serial.is_empty() && p.serial_number.as_deref() == Some(&usbdev.serial)
            }) {
                Some(p) => p,
                None if probes.len() == 1 => &probes[0],
                None => return Err("Cannot tell this base from the other ST-LINKs".into()),
            };
            let selector = DebugProbeSelector {
                vendor_id: info.vendor_id,
                product_id: info.product_id,
                interface: None,
                serial_number: info.serial_number.clone(),
            };
            StLinkFactory
                .open(&selector)
                .map(Probe::from_specific_probe)
                .map_err(|e| e.to_string())
        }
    }
}

/// Opens a session on `chip` under reset, with [`TagSequence`].
///
/// probe-rs's attach under reset holds nRST, attaches, arms reset vector catch
/// and releases nRST, so the core is halted at its reset vector before any
/// firmware instruction runs. Dropping the [`Session`] detaches through
/// [`TagSequence`], which lets the core run.
///
/// # Errors
///
/// A message if probe-rs has no target named `chip`, the probe cannot be
/// opened ([`open_probe`]), or the attach fails.
fn session(usbdev: &UsbDev, driver: Driver, chip: &str) -> Result<Session, String> {
    let registry = Registry::from_builtin_families();
    let mut target = registry
        .get_target_by_name(chip)
        .map_err(|e| format!("probe-rs has no target {chip}: {e}"))?;
    target.debug_sequence = DebugSequence::Arm(Arc::new(TagSequence));
    open_probe(usbdev, driver)?
        .attach_under_reset_with_registry(
            TargetSelector::Specified(target),
            Permissions::default(),
            &registry,
        )
        .map_err(|e| format!("Cannot attach to the tag: {e}"))
}

/// The internal-flash erase that goes with a program.
///
/// Every variant also erases whatever sectors the image itself covers; they
/// differ in what else is cleared. Internal flash only: a tag's external
/// flash log is never touched here.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Erase {
    /// Only the sectors the image covers (qtprogram's "none").
    ///
    /// Anything outside the image, such as calibration constants stored
    /// above it, is kept.
    ImagePages,
    /// Flash `start..end` first (qtprogram's "below calibration").
    ///
    /// Erases the sectors overlapping the range, then programs as
    /// [`Erase::ImagePages`] in the same session.
    Range {
        /// First address to erase, inclusive (an absolute flash address,
        /// e.g. `0x0800_0000`).
        start: u64,
        /// End of the range, exclusive.
        end: u64,
    },
    /// The whole chip (qtprogram's "all").
    ///
    /// Destroys any calibration stored in internal flash.
    All,
}

/// Progress during [`program`].
///
/// Delivered on the calling thread, in order, from inside probe-rs's flash
/// loop.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Progress {
    /// An operation started: "Reading back", "Erasing", "Programming" or
    /// "Verifying".
    Step(&'static str),
    /// Bytes done of a total, for the step under way.
    ///
    /// `total` is 0 when probe-rs did not announce a size for the step.
    Bytes {
        /// Bytes completed so far in this step.
        done: u64,
        /// Bytes the step will cover, or 0 if unknown.
        total: u64,
    },
}

/// Returns the step name reported in [`Progress::Step`] for a probe-rs flash
/// operation.
///
/// `Fill` is probe-rs reading back flash it must preserve inside a sector it
/// is about to erase, hence "Reading back".
fn operation_name(op: ProgressOperation) -> &'static str {
    match op {
        ProgressOperation::Fill => "Reading back",
        ProgressOperation::Erase => "Erasing",
        ProgressOperation::Program => "Programming",
        ProgressOperation::Verify => "Verifying",
    }
}

/// Erases as `erase` says, programs and verifies `elf` in one session, clears
/// FLASH_SR.PEMPTY on an STM32L4 if it is set, and starts the image with a
/// debugger jump to its vector table (STM32_Programmer_CLI's `-g
/// 0x08000000`). One session, so no reset falls between the erase and the
/// program.
///
/// The counterpart of qtprogram's STM32_Programmer_CLI program call. `chip`
/// comes from [`chip_for`]; `on_progress` is called synchronously with
/// [`Progress`] events. Blocks until the image is running or an error ends
/// the session; a full L432 image takes seconds, longer through
/// [`Driver::FieldSafe`].
///
/// The tag is held in reset to attach, so whatever it was doing ends. The
/// image is started by setting SP and PC from the vector table and resuming
/// the core, not by a reset, so the first boot after programming is not a
/// cold boot; the tag's next reset boots it normally.
///
/// # Errors
///
/// A message if the attach fails, the range erase fails, programming or
/// verification fails, PEMPTY will not clear, or the core cannot be started.
/// A failure after the erase leaves the tag with partly erased or programmed
/// flash, which another program call repairs.
pub fn program(
    usbdev: &UsbDev,
    driver: Driver,
    chip: &str,
    elf: &Path,
    erase: Erase,
    mut on_progress: impl FnMut(Progress),
) -> Result<(), String> {
    let mut session = session(usbdev, driver, chip)?;
    let mut totals = std::collections::HashMap::new();
    let mut done = std::collections::HashMap::new();
    {
        let mut progress = FlashProgress::new(|event| match event {
            ProgressEvent::AddProgressBar { operation, total } => {
                totals.insert(operation as u8, total.unwrap_or(0));
            }
            ProgressEvent::Started(op) => {
                done.insert(op as u8, 0u64);
                on_progress(Progress::Step(operation_name(op)));
            }
            ProgressEvent::Progress {
                operation, size, ..
            } => {
                let d = done.entry(operation as u8).or_insert(0);
                *d += size;
                on_progress(Progress::Bytes {
                    done: *d,
                    total: *totals.get(&(operation as u8)).unwrap_or(&0),
                });
            }
            _ => {}
        });
        if let Erase::Range { start, end } = erase {
            flashing::erase(&mut session, &mut progress, start, end, false)
                .map_err(|e| format!("Erase failed: {e}"))?;
        }
        let mut options = DownloadOptions::default();
        options.verify = true;
        options.do_chip_erase = erase == Erase::All;
        options.progress = progress;
        flashing::download_file_with_options(
            &mut session,
            elf,
            ElfLoader(Default::default()),
            options,
        )
        .map_err(|e| format!("Programming failed: {e}"))?;
    }
    let mut core = session.core(0).map_err(|e| e.to_string())?;
    clear_l4_pempty(&mut core)?;
    // Start as `-g 0x08000000` does: SP and PC from the vector table.
    let sp = core.read_word_32(FLASH_BASE).map_err(|e| e.to_string())?;
    let pc = core
        .read_word_32(FLASH_BASE + 4)
        .map_err(|e| e.to_string())?;
    let sp_reg = core.stack_pointer();
    let pc_reg = core.program_counter();
    core.write_core_reg(sp_reg, sp).map_err(|e| e.to_string())?;
    core.write_core_reg(pc_reg, pc & !1)
        .map_err(|e| e.to_string())?;
    core.run()
        .map_err(|e| format!("Cannot start the image: {e}"))?;
    Ok(())
}

/// On an STM32L4, clears FLASH_SR.PEMPTY if it is set. Writing 1 toggles the
/// bit, so it is written only when found set, then read back; the rule of
/// `embedded/tools/flash_release.py`. Does nothing on other parts.
///
/// The part is recognised by reading the L4 `DBGMCU_IDCODE` address; a read
/// that fails (as it may on a U3) counts as "not an L4".
///
/// # Errors
///
/// A message if FLASH_SR cannot be read or written, or if PEMPTY is still set
/// after the write, in which case the tag would boot its ROM bootloader.
fn clear_l4_pempty(core: &mut probe_rs::Core<'_>) -> Result<(), String> {
    let l4 = core
        .read_word_32(u64::from(L4_DBGMCU_IDCODE))
        .is_ok_and(|id| (id & 0xfff) as u16 == DEV_L432);
    if !l4 {
        return Ok(());
    }
    let sr = core.read_word_32(L4_FLASH_SR).map_err(|e| e.to_string())?;
    if sr & L4_FLASH_SR_PEMPTY == 0 {
        return Ok(());
    }
    core.write_word_32(L4_FLASH_SR, L4_FLASH_SR_PEMPTY)
        .map_err(|e| e.to_string())?;
    let after = core.read_word_32(L4_FLASH_SR).map_err(|e| e.to_string())?;
    if after & L4_FLASH_SR_PEMPTY != 0 {
        return Err("FLASH_SR.PEMPTY is still set; the tag would boot its ROM bootloader".into());
    }
    Ok(())
}

/// Mass-erases the tag's internal flash and programs nothing (STM32_Programmer_CLI's
/// `-e all`).
///
/// `chip` comes from [`chip_for`]. The tag is held in reset to attach and is
/// left with empty flash: an STM32L4 then latches FLASH_SR.PEMPTY at its next
/// reset and boots its ROM bootloader until it is programmed. Calibration
/// stored in internal flash is destroyed; the external flash log is not
/// touched.
///
/// # Errors
///
/// A message if the attach or the erase fails.
pub fn erase_all(usbdev: &UsbDev, driver: Driver, chip: &str) -> Result<(), String> {
    let mut session = session(usbdev, driver, chip)?;
    flashing::erase_all(&mut session, &mut FlashProgress::empty(), false)
        .map_err(|e| format!("Erase failed: {e}"))
}

/// Reads `len` bytes of tag memory at `addr` in a session under reset (the
/// core is held at its reset vector, so nothing runs while it is read).
///
/// `addr` is an absolute target address (flash at `0x0800_0000`, SRAM, or a
/// register), at any alignment; `chip` comes from [`chip_for`]. Because the
/// core is halted at the reset vector, SRAM holds what survived the reset,
/// not what the firmware would initialise. Through [`Driver::FieldSafe`],
/// unaligned edges are read as whole words, so reading a peripheral register
/// with read side effects can disturb its neighbours.
///
/// When the session closes the core is released and the tag boots its
/// firmware, as STM32_Programmer_CLI leaves it; the call waits 50 ms after
/// that before returning.
///
/// # Errors
///
/// A message if the attach fails or the read faults (an unmapped address,
/// for example).
pub fn read_memory(
    usbdev: &UsbDev,
    driver: Driver,
    chip: &str,
    addr: u64,
    len: usize,
) -> Result<Vec<u8>, String> {
    let mut session = session(usbdev, driver, chip)?;
    let mut core = session.core(0).map_err(|e| e.to_string())?;
    let mut buf = vec![0u8; len];
    core.read_8(addr, &mut buf)
        .map_err(|e| format!("Read failed: {e}"))?;
    drop(core);
    drop(session);
    // Leave the tag running its firmware, as STM32_Programmer_CLI does.
    std::thread::sleep(Duration::from_millis(50));
    Ok(buf)
}

/// Hardware-free checks of target and driver selection.
#[cfg(test)]
mod tests {
    use super::*;

    /// Checks that [`chip_for`] maps device ID and flash size to the right
    /// target and refuses an unknown device.
    #[test]
    fn chips_follow_device_and_flash_size() {
        assert_eq!(chip_for(DEV_L432, Some(0x40000)), Ok("STM32L432KC"));
        assert_eq!(chip_for(DEV_U375, Some(0x100000)), Ok("STM32U375RG"));
        assert!(chip_for(0x123, None).is_err());
    }

    /// Checks that any identity string selects the stock driver and none
    /// selects the field-safe one.
    #[test]
    fn driver_follows_identity() {
        assert_eq!(Driver::for_identity(Some("base=x")), Driver::Stock);
        assert_eq!(Driver::for_identity(None), Driver::FieldSafe);
    }
}
