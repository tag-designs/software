//! Small rules about a tag that Tag Monitor's commands and displays share.
//!
//! Port of `host/applications/qtmon/core/tagrules.h`: which states can be
//! downloaded, how far an erase has to go, how far the tag's clock is off,
//! the oldest Tag Monitor a tag accepts, and how a base is listed (with,
//! beyond qtmonitor, what the base says it is).

use tagcore::UsbDev;
use tagcore::proto::{Status, TagState, TagType};

/// External flash erase sector, bytes, for tags that do not report one.
pub const DEFAULT_SECTOR_BYTES: i64 = 4096;

/// Bytes per IMUTag data-log page, used only to estimate an erase's length
/// for older IMUTag firmware, which does not report the erase total.
pub const IMU_DATA_LOG_PAGE_BYTES: i64 = 2048;

/// The oldest Tag Monitor version a tag may require.
///
/// The tag can name it (`TagInfo.qtmonitor_min_version`), but no firmware
/// sets it yet, so this fixed floor stands in for it.
pub const REQUIRED_MONITOR_VERSION: f32 = 2.0;

/// Whether a tag in `state` can be downloaded: FINISHED, ABORTED and
/// EXCEPTION. EXCEPTION is included because the tag is no longer collecting,
/// and downloading avoids a reset that could erase the evidence.
pub fn is_downloadable_state(state: TagState) -> bool {
    matches!(
        state,
        TagState::Finished | TagState::Aborted | TagState::Exception
    )
}

/// The number of erase sectors needed to cover `bytes`, rounded up; 0 when
/// either argument is zero or negative, so a tag with no geometry shows no
/// erase progress.
pub fn rounded_sector_count(bytes: i64, sector_size: i64) -> i32 {
    if bytes <= 0 || sector_size <= 0 {
        return 0;
    }
    ((bytes + sector_size - 1) / sector_size) as i32
}

/// How many sectors an erase will report, for its progress bar.
///
/// The tag's own total when it reports one (`erase_sectors_total_plus_one`);
/// otherwise, for an IMUTag, an estimate from its page count; otherwise the
/// sectors in the whole external flash.
pub fn erase_sector_maximum(
    status: &Status,
    tag_type: TagType,
    external_flash_bytes: i64,
    sector_size: i64,
) -> i32 {
    if status.erase_sectors_total_plus_one > 0 {
        return status.erase_sectors_total_plus_one - 1;
    }
    if tag_type == TagType::Imutag {
        return rounded_sector_count(
            i64::from(status.external_data_count) * IMU_DATA_LOG_PAGE_BYTES,
            sector_size,
        );
    }
    rounded_sector_count(external_flash_bytes, sector_size)
}

/// The tag's clock minus the host's, in seconds.
///
/// `tag_millis` is `Status.millis`, `host_millis` the host's clock when the
/// status was read, both milliseconds since the epoch. Positive when the tag
/// is ahead; an unset tag clock (near zero) gives about -1.45e9 s. Computed
/// in `f64` throughout, as qtmonitor does.
pub fn clock_offset_seconds(tag_millis: i64, host_millis: i64) -> f64 {
    (tag_millis as f64 - host_millis as f64) / 1000.0
}

/// Whether Tag Monitor `monitor_version` may attach to tags: false when it
/// is older than [`REQUIRED_MONITOR_VERSION`].
pub fn monitor_version_accepted(monitor_version: f32) -> bool {
    monitor_version >= REQUIRED_MONITOR_VERSION
}

/// How a base is listed when there are several to choose from:
/// `"BBB:AAA    0xVID:0xPID"`, bus and address zero-padded to three digits,
/// the IDs in hexadecimal.
pub fn describe_base(base: &UsbDev) -> String {
    format!(
        "{:03}:{:03}    0x{:x}:0x{:x}",
        base.bus, base.address, base.vid, base.pid
    )
}

/// One field of a base identity string (`key=value;...`).
fn identity_field<'a>(identity: &'a str, key: &str) -> Option<&'a str> {
    identity
        .split(';')
        .find_map(|f| f.strip_prefix(key)?.strip_prefix('='))
        .filter(|v| !v.is_empty())
}

/// A base in the list to choose from: [`describe_base`], then what the base
/// says it is, so the base with the tag can be picked out:
/// `"002:005    0x483:0x3748    BITTAG_BASE_V7 fw a1b2c3d+  (ST-LINK 0037...)"`.
/// The firmware hash gets `+` when it was built from a dirty tree. A base
/// that does not identify itself shows its ST-LINK serial only.
///
/// Not in qtmonitor, which lists [`describe_base`] alone.
pub fn describe_base_with_identity(base: &UsbDev, identity: Option<&str>) -> String {
    let mut text = describe_base(base);
    if let Some(id) = identity {
        text += &format!("    {}", identity_field(id, "base").unwrap_or("tag base"));
        if let Some(fw) = identity_field(id, "fw") {
            let dirty = if identity_field(id, "dirty") == Some("1") {
                "+"
            } else {
                ""
            };
            text += &format!(" fw {fw}{dirty}");
        }
    }
    if !base.serial.is_empty() {
        text += &format!("  (ST-LINK {})", base.serial);
    }
    text
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A base that identifies itself, one built from a dirty tree, and one
    /// that predates the identify command.
    #[test]
    fn a_base_is_listed_with_its_identity() {
        let base = UsbDev {
            bus: 2,
            address: 5,
            vid: 0x483,
            pid: 0x3748,
            serial: "0037ABC".into(),
            ..UsbDev::default()
        };
        assert_eq!(
            describe_base_with_identity(
                &base,
                Some("base=BITTAG_BASE_V7;fw=a1b2c3d;dirty=1;date=2026-10-27")
            ),
            "002:005    0x483:0x3748    BITTAG_BASE_V7 fw a1b2c3d+  (ST-LINK 0037ABC)"
        );
        assert_eq!(
            describe_base_with_identity(&base, Some("base=U375_BASE;fw=9f171a2;dirty=0")),
            "002:005    0x483:0x3748    U375_BASE fw 9f171a2  (ST-LINK 0037ABC)"
        );
        assert_eq!(
            describe_base_with_identity(&base, None),
            "002:005    0x483:0x3748  (ST-LINK 0037ABC)"
        );
    }
}
