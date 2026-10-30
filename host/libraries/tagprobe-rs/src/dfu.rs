//! Programming a base through its MCU's ROM bootloader over USB DFU.
//!
//! The bases' MCUs (STM32F042, STM32C071, STM32L432) start their ROM
//! bootloader when the base is plugged in with its button held, which holds
//! BOOT0. The bootloader enumerates as `0483:df11` and speaks **DfuSe**, ST's
//! extension of DFU 1.1 (bcdDFU `0x011A`): DFU_DNLOAD block 0 carries
//! commands (Set Address Pointer `0x21`, Erase `0x41`), data blocks from 2 on
//! land at *pointer + (block - 2) x transfer size*, and each alternate
//! setting's string gives a memory map such as
//! `@Internal Flash  /0x08000000/032*0001Kg`.
//!
//! `dfu-core` does the DfuSe work: it erases the pages the image covers, sets
//! the address once and writes the blocks. After the last block it sends the
//! zero-length DFU_DNLOAD that ends the download; the bootloader then
//! manifests and jumps to the address pointer, still the image's start, and
//! leaves the bus. This is `dfu-util -a 0 -i 0 -s 0x08000000:leave -D`.
//!
//! [`program`] writes alternate setting 0 only, after checking that it is
//! internal flash, so option bytes (alternate setting 1) are never written.
//! The design is in
//! `host/commandline-rs/design/proposals/programming-without-cubeprogrammer.md`.

use std::path::Path;
use std::time::Duration;

use dfu_nusb::DfuNusb;
use nusb::MaybeFuture;
use object::read::elf::{FileHeader, ProgramHeader};

/// USB vendor ID of the STM32 ROM bootloader in DFU mode.
pub const DFU_VID: u16 = 0x0483;
/// USB product ID of the STM32 ROM bootloader in DFU mode.
pub const DFU_PID: u16 = 0xdf11;
/// The bootloader's DFU interface.
const DFU_INTERFACE: u8 = 0;
/// The alternate setting for internal flash.
const FLASH_ALT: u8 = 0;
/// How long a string descriptor read may take.
const STRING_TIMEOUT: Duration = Duration::from_secs(3);

/// A device in DFU mode, as listed by [`list`].
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DfuDevice {
    /// The bootloader's USB serial number, or empty when it has none.
    ///
    /// Not the base's ST-LINK serial: the ROM bootloader derives its own.
    pub serial: String,
    /// The USB bus the device is on, as the operating system names it.
    pub bus: String,
    /// The USB device address on that bus; changes on replug.
    pub address: u8,
}

/// Lists the devices in STM32 DFU mode (`0483:df11`). Opens nothing.
pub fn list() -> Vec<DfuDevice> {
    let Ok(devices) = nusb::list_devices().wait() else {
        return Vec::new();
    };
    devices
        .filter(|d| d.vendor_id() == DFU_VID && d.product_id() == DFU_PID)
        .map(|d| DfuDevice {
            serial: d.serial_number().unwrap_or_default().to_string(),
            bus: d.bus_id().to_string(),
            address: d.device_address(),
        })
        .collect()
}

/// One region of a DfuSe memory map: where it starts and how many bytes it
/// holds.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Region {
    /// First address.
    pub start: u32,
    /// Size in bytes.
    pub size: u32,
}

/// Parses a DfuSe alternate-setting string into its name and region, e.g.
/// `@Internal Flash  /0x08000000/032*0001Kg` into `"Internal Flash"` and
/// 32 KB at `0x0800_0000`.
///
/// The sector list is `count*size<unit><access>` items separated by commas;
/// the unit is `K` (1024), `M` (1024 x 1024) or a space or `B` (bytes).
/// Only strings with one address are accepted, which is every STM32 flash
/// alternate setting.
///
/// # Errors
///
/// A message naming the string when it is not in this form.
pub fn parse_region(text: &str) -> Result<(String, Region), String> {
    let bad = || format!("unexpected DfuSe memory map \"{text}\"");
    let body = text.strip_prefix('@').ok_or_else(bad)?;
    let mut parts = body.split('/');
    let name = parts.next().ok_or_else(bad)?.trim().to_string();
    let address = parts.next().ok_or_else(bad)?.trim();
    let sectors = parts.next().ok_or_else(bad)?;
    if parts.next().is_some_and(|rest| !rest.is_empty()) {
        return Err(bad());
    }
    let start = address
        .strip_prefix("0x")
        .or_else(|| address.strip_prefix("0X"))
        .and_then(|hex| u32::from_str_radix(hex, 16).ok())
        .ok_or_else(bad)?;
    let mut size: u64 = 0;
    for item in sectors.split(',') {
        let (count, rest) = item.trim().split_once('*').ok_or_else(bad)?;
        let count: u64 = count.trim().parse().map_err(|_| bad())?;
        let digits = rest.find(|c: char| !c.is_ascii_digit()).ok_or_else(bad)?;
        let each: u64 = rest[..digits].parse().map_err(|_| bad())?;
        let unit = match rest[digits..].chars().next() {
            Some('K') => 1024,
            Some('M') => 1024 * 1024,
            Some(' ' | 'B') => 1,
            _ => return Err(bad()),
        };
        size += count * each * unit;
    }
    let size = u32::try_from(size).map_err(|_| bad())?;
    Ok((name, Region { start, size }))
}

/// Reads an ELF's loadable contents as one image for flash: the bytes of
/// every `PT_LOAD` segment with file contents, placed at its physical (load)
/// address, from the lowest to the highest, gaps filled with `0xFF`. What
/// `objcopy -O binary` makes.
///
/// Returns the image's start address and bytes.
///
/// # Errors
///
/// A message when the file cannot be read, is not a 32-bit little-endian
/// ELF, or has nothing to load.
pub fn flatten_elf(path: &Path) -> Result<(u32, Vec<u8>), String> {
    let name = path.display();
    let data = std::fs::read(path).map_err(|e| format!("{name}: {e}"))?;
    let header = object::elf::FileHeader32::<object::Endianness>::parse(&*data)
        .map_err(|e| format!("{name} is not a 32-bit ELF: {e}"))?;
    let endian = header.endian().map_err(|e| format!("{name}: {e}"))?;
    let mut segments = Vec::new();
    for ph in header
        .program_headers(endian, &*data)
        .map_err(|e| format!("{name}: {e}"))?
    {
        if ph.p_type(endian) != object::elf::PT_LOAD || ph.p_filesz(endian) == 0 {
            continue;
        }
        let bytes = ph
            .data(endian, &*data)
            .map_err(|_| format!("{name}: a segment lies outside the file"))?;
        segments.push((ph.p_paddr(endian), bytes));
    }
    let start = segments
        .iter()
        .map(|(a, _)| *a)
        .min()
        .ok_or_else(|| format!("{name} has nothing to load"))?;
    let end = segments
        .iter()
        .map(|(a, b)| *a as u64 + b.len() as u64)
        .max()
        .unwrap_or(start as u64);
    let mut image = vec![0xffu8; (end - start as u64) as usize];
    for (address, bytes) in segments {
        let at = (address - start) as usize;
        image[at..at + bytes.len()].copy_from_slice(bytes);
    }
    Ok((start, image))
}

/// Opens a device, claims the DFU interface, selects the flash alternate
/// setting and returns it with that setting's name and region.
///
/// # Errors
///
/// A message when the device cannot be opened or claimed (on Linux, a
/// missing udev rule; on Windows, no WinUSB driver), or its alternate
/// setting cannot be read.
fn open(device: &DfuDevice) -> Result<(DfuNusb, String, Region), String> {
    let info = nusb::list_devices()
        .wait()
        .map_err(|e| e.to_string())?
        .find(|d| {
            d.vendor_id() == DFU_VID
                && d.product_id() == DFU_PID
                && d.bus_id() == device.bus
                && d.device_address() == device.address
        })
        .ok_or("The DFU device has gone from USB")?;
    let usb = info
        .open()
        .wait()
        .map_err(|e| format!("Cannot open the DFU device: {e}"))?;
    let interface = usb
        .claim_interface(DFU_INTERFACE)
        .wait()
        .map_err(|e| format!("Cannot claim the DFU interface: {e}"))?;
    // nusb's futures need a Tokio reactor when awaited (its tokio feature is
    // on, through dfu-nusb), so the two async steps run on a small runtime.
    let runtime = tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .map_err(|e| format!("Cannot start the USB runtime: {e}"))?;
    let label = runtime.block_on(alt_string(&usb, &interface, FLASH_ALT))?;
    let (name, region) = parse_region(&label)?;
    let dfu = runtime
        .block_on(DfuNusb::open(usb, interface, FLASH_ALT))
        .map_err(|e| format!("Not an STM32 DFU device: {e}"))?;
    Ok((dfu, name, region))
}

/// Reads alternate setting `alt`'s string, the DfuSe memory map.
///
/// # Errors
///
/// A message when the setting has no string or it cannot be read.
async fn alt_string(
    usb: &nusb::Device,
    interface: &nusb::Interface,
    alt: u8,
) -> Result<String, String> {
    let index = interface
        .descriptors()
        .find(|a| a.alternate_setting() == alt)
        .and_then(|a| a.string_index())
        .ok_or("The DFU device names no memory for alternate setting 0")?;
    let lang = usb
        .get_string_descriptor_supported_languages(STRING_TIMEOUT)
        .await
        .map_err(|e| e.to_string())?
        .next()
        .unwrap_or_default();
    usb.get_string_descriptor(index, lang, STRING_TIMEOUT)
        .await
        .map_err(|e| format!("Cannot read the DFU memory map: {e}"))
}

/// Writes `elf` to a base's internal flash through its ROM bootloader and
/// leaves DFU mode, so the base starts the new image.
///
/// Checks first that alternate setting 0 is internal flash and that the
/// image lies inside it; the bootloader then erases only the pages the image
/// covers. `on_progress` is called with the bytes written so far and the
/// total.
///
/// Leaving DFU makes the device drop off the bus, so a USB error after every
/// byte has been written is taken as the bootloader leaving, not as a
/// failure; the caller confirms the result by finding the base again on USB.
///
/// # Errors
///
/// A message when the image cannot be read, the device cannot be opened, its
/// alternate setting 0 is not internal flash, the image does not fit in it,
/// or the download fails before the last byte.
pub fn program(
    device: &DfuDevice,
    elf: &Path,
    mut on_progress: impl FnMut(usize, usize) + 'static,
) -> Result<(), String> {
    let (start, image) = flatten_elf(elf)?;
    let (dfu, name, region) = open(device)?;
    if !name.eq_ignore_ascii_case("Internal Flash") {
        return Err(format!(
            "alternate setting 0 is \"{name}\", not internal flash; nothing was written"
        ));
    }
    let end = start as u64 + image.len() as u64;
    if start < region.start || end > region.start as u64 + region.size as u64 {
        return Err(format!(
            "the image (0x{start:08x}-0x{end:08x}) does not fit in internal flash \
             (0x{:08x}, {} KB); nothing was written",
            region.start,
            region.size / 1024
        ));
    }
    let total = image.len();
    let written = std::rc::Rc::new(std::cell::Cell::new(0usize));
    let counter = written.clone();
    let mut dfu = dfu.into_sync_dfu();
    dfu.override_address(start).with_progress(move |n| {
        counter.set(counter.get() + n);
        on_progress(counter.get(), total);
    });
    match dfu.download_from_slice(&image) {
        Ok(_) => Ok(()),
        Err(e) if written.get() >= total => {
            log::debug!("after the last block: {e} (the bootloader leaving)");
            Ok(())
        }
        Err(e) => Err(format!(
            "DFU download failed after {} of {total} bytes: {e}",
            written.get()
        )),
    }
}

/// Hardware-free checks of the memory-map parser and the ELF flattening.
#[cfg(test)]
mod tests {
    use super::*;

    /// Checks the STM32F042's internal-flash string and a multi-sector one.
    #[test]
    fn regions_parse() {
        assert_eq!(
            parse_region("@Internal Flash  /0x08000000/032*0001Kg"),
            Ok((
                "Internal Flash".into(),
                Region {
                    start: 0x0800_0000,
                    size: 32 * 1024
                }
            ))
        );
        assert_eq!(
            parse_region("@Internal Flash  /0x08000000/04*016Kg,01*064Kg,07*128Kg")
                .unwrap()
                .1
                .size,
            (4 * 16 + 64 + 7 * 128) * 1024
        );
        assert_eq!(
            parse_region("@Option Bytes  /0x1FFFF800/01*016 e")
                .unwrap()
                .1,
            Region {
                start: 0x1fff_f800,
                size: 16
            }
        );
        assert!(parse_region("Internal Flash").is_err());
        assert!(parse_region("@Internal Flash  /0x08000000/32*1Xg").is_err());
    }
}
