//! Finding ST-LINK probes on USB and opening the one a [`UsbDev`] selects.
//!
//! Listing reads each probe's serial number but claims no interface and
//! sends nothing over SWD, so it never resets a tag.

use std::time::Duration;

use log::{debug, error};
use rusb::{Context, Device, DeviceDescriptor, UsbContext};

use super::{LinkAdapt, libusb_name, stlink};

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

impl LinkAdapt {
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
                error!("USB Init error {}", libusb_name(&e));
                return Vec::new();
            }
        };
        let mut stlinks = Vec::new();
        for dev in devices.iter() {
            let desc = match dev.device_descriptor() {
                Ok(desc) => desc,
                Err(e) => {
                    error!("USB Init error {}", libusb_name(&e));
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

    /// Finds and opens the device `usbdev` selects, and sets the endpoints.
    pub(super) fn open_matching(&mut self, usbdev: &UsbDev) -> bool {
        let Some(ctx) = &self.ctx else {
            return false;
        };
        let devices = match ctx.devices() {
            Ok(devices) => devices,
            Err(e) => {
                error!("USB Init error {}", libusb_name(&e));
                return false;
            }
        };
        for dev in devices.iter() {
            let desc = match dev.device_descriptor() {
                Ok(desc) => desc,
                Err(e) => {
                    error!("USB Init error {}", libusb_name(&e));
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
                    error!("USB Open error {}", libusb_name(&e));
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
}
