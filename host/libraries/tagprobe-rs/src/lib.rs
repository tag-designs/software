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
//! `docs/investigations/2026-10-probe-rs-evaluation.md`.

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
pub const PROBE_RS_VERSION: &str = "0.32.0";

/// STM32L43x/L44x DBGMCU device ID.
pub const DEV_L432: u16 = 0x435;
/// STM32U37x/U38x DBGMCU device ID.
pub const DEV_U375: u16 = 0x454;

const L4_DBGMCU_IDCODE: u32 = 0xE004_2000;
const U3_DBGMCU_IDCODE: u32 = 0xE004_4000;
const L4_FLASH_SR: u64 = 0x4002_2010;
const L4_FLASH_SR_PEMPTY: u32 = 1 << 17;
const FLASH_BASE: u64 = 0x0800_0000;

/// Which probe-rs driver drives a base.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Driver {
    /// probe-rs's own ST-LINK driver: the base answers the identify command,
    /// so it has the ST-LINK fixes.
    Stock,
    /// [`FieldSafeProbe`]: the base predates the identify command.
    FieldSafe,
}

impl Driver {
    /// The driver for a base with this identity (from [`identify`]).
    pub fn for_identity(identity: Option<&str>) -> Self {
        if identity.is_some() {
            Driver::Stock
        } else {
            Driver::FieldSafe
        }
    }

    /// Short description, e.g. for a status line.
    pub fn name(self) -> &'static str {
        match self {
            Driver::Stock => "probe-rs ST-LINK driver",
            Driver::FieldSafe => "field-safe driver",
        }
    }
}

/// The ST-LINK-compatible probes on USB (bases and genuine ST-LINKs alike).
pub fn list() -> Vec<UsbDev> {
    LinkAdapt::new().available()
}

/// Asks a base who it is with `F6 99`; `None` for a base that predates the
/// command or a genuine ST-LINK. Opens the base without touching nRST or
/// SWD, so the tag is not reset.
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
#[derive(Debug, Clone, PartialEq)]
pub struct Connect {
    /// DBGMCU device ID, e.g. [`DEV_L432`].
    pub device_id: u16,
    /// The part family's name, as STM32CubeProgrammer prints it.
    pub device_name: &'static str,
    /// Target supply voltage measured by the base.
    pub voltage: Option<f32>,
}

/// The family name for a DBGMCU device ID.
pub fn device_name(device_id: u16) -> &'static str {
    match device_id {
        DEV_L432 => "STM32L43xxx/STM32L44xxx",
        DEV_U375 => "STM32U37xx/STM32U38xx",
        _ => "unknown STM32",
    }
}

/// The probe-rs target for a device, from its ID and the flash size its
/// image was linked for (`__flash0_end__ - __flash0_base__`).
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
    link.assert_reset(true);
    link.detach();
    match id {
        Some(idcode) => {
            let device_id = (idcode & 0xfff) as u16;
            Ok(Connect {
                device_id,
                device_name: device_name(device_id),
                voltage,
            })
        }
        None => Err("No STM32 target found".into()),
    }
}

/// The attach/detach sequence used for every session: the ARM default, which
/// writes nothing to DBGMCU and on detach clears DHCSR.C_DEBUGEN and DEMCR.
#[derive(Debug)]
pub struct TagSequence;

impl ArmDebugSequence for TagSequence {}

/// Opens `usbdev` with `driver`, as a probe-rs [`Probe`].
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
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Erase {
    /// Only the sectors the image covers (qtprogram's "none").
    ImagePages,
    /// Flash `start..end` first (qtprogram's "below calibration").
    Range { start: u64, end: u64 },
    /// The whole chip (qtprogram's "all").
    All,
}

/// Progress during [`program`].
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Progress {
    /// An operation started: "Erasing", "Programming", "Verifying".
    Step(&'static str),
    /// Bytes done of a total, for the step under way.
    Bytes { done: u64, total: u64 },
}

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

/// Reads `len` bytes of tag memory at `addr` in a session under reset (the
/// core is held at its reset vector, so nothing runs while it is read).
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn chips_follow_device_and_flash_size() {
        assert_eq!(chip_for(DEV_L432, Some(0x40000)), Ok("STM32L432KC"));
        assert_eq!(chip_for(DEV_U375, Some(0x100000)), Ok("STM32U375RG"));
        assert!(chip_for(0x123, None).is_err());
    }

    #[test]
    fn driver_follows_identity() {
        assert_eq!(Driver::for_identity(Some("base=x")), Driver::Stock);
        assert_eq!(Driver::for_identity(None), Driver::FieldSafe);
    }
}
