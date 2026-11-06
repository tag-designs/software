//! The tag identity record, parsed from a captured internal-flash image.
//!
//! Port of `tagcore/recovery/identityrecord.cc`. The record
//! (`embedded/tags/common/core/inc/tag_identity.h`) sits directly after the
//! interrupt vectors, at [`McuMap::identity_offset`](super::McuMap): an
//! 8-byte header (`"TGID"` magic, `u16` format version, `u16` total size)
//! then TLV entries `{u16 id, u16 len, value}` ending in an `0xFFFF` entry.
//! It has no CRC; a record is accepted only when its magic, its declared
//! size and its end entry all agree. The host-side counterpart of
//! `embedded/tools/decode_tag_identity.py`, limited to what a capture and a
//! rebuild need.
//!
//! Tag Programmer and `tag-flash` read records with this parser too, from an
//! image's flash or from a tag's: [`find_identity_record`] tries each MCU's
//! offset in turn.

use std::collections::BTreeMap;

use super::mcu::ALL_MCUS;

/// The header magic, `"TGID"` read as a little-endian `u32`.
const MAGIC: u32 = 0x4449_4754;
/// Entry id of the end entry.
const END: u16 = 0xFFFF;
/// Entry id of the numbers block.
const NUMBERS: u16 = 0x0201;
/// Entry id of the BackupState map.
const BACKUP_STATE: u16 = 0x0401;
/// Entry id of the external flash description.
const EXTERNAL_FLASH: u16 = 0x0402;
/// Entry id of the default configuration's location.
const DEFAULT_CONFIG: u16 = 0x0309;
/// Entry id of the session facts' location.
const SESSION_FACTS: u16 = 0x0601;

/// The name a string entry id is stored under, as in
/// `decode_tag_identity.py`; `None` for an id that is not a string.
fn string_name(id: u16) -> Option<&'static str> {
    Some(match id {
        0x0101 => "target",
        0x0102 => "family",
        0x0103 => "board",
        0x0104 => "board_desc",
        0x0105 => "board_revision",
        0x0106 => "firmware",
        0x0107 => "git_repo",
        0x0108 => "git_hash",
        0x0109 => "git_sha",
        0x010A => "git_date",
        0x010B => "source_path",
        0x010C => "flash_part",
        0x010D => "rtc_part",
        0x010E => "loader",
        0x010F => "decoder",
        0x0110 => "monitor_version",
        _ => return None,
    })
}

/// The name a region entry id is stored under; `None` for another id.
fn region_name(id: u16) -> Option<&'static str> {
    Some(match id {
        0x0301 => "image",
        0x0302 => "persistent",
        0x0303 => "state_log",
        0x0304 => "stored_config",
        0x0305 => "data_headers",
        0x0306 => "calibration",
        0x0307 => "nand_map",
        0x0308 => "scratchpad",
        _ => return None,
    })
}

/// One region entry: where a structure lives in internal flash.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct IdentityRegion {
    /// First byte.
    pub start: u32,
    /// One past the last byte; 0 means to the persistent end.
    pub end: u32,
    /// Bytes per record, or 0 for an untyped region.
    pub record_size: u32,
    /// Records, or 0 for "until an erased one".
    pub record_count: u32,
    /// Layout version of the records.
    pub layout_version: u32,
}

/// Where the BackupState words live in the backup registers.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct IdentityBackupState {
    /// Address of word 0.
    pub base: u32,
    /// Words in the mirror.
    pub word_count: u32,
    /// Value of the valid word when retained state is valid.
    pub valid_magic: u32,
    /// Index of the valid word.
    pub word_valid: u32,
    /// Index of the state word.
    pub word_state: u32,
    /// Index of the internal page or checkpoint count.
    pub word_pages: u32,
    /// Index of the external block count.
    pub word_external_blocks: u32,
    /// Index of the reset cause.
    pub word_reset_cause: u32,
}

/// The external flash part, as the firmware knows it.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct IdentityExternalFlash {
    /// Manufacturer << 16 | device.
    pub jedec_id: u32,
    /// Data area in bytes.
    pub flash_size: u32,
    /// Program page in bytes.
    pub program_page: u32,
    /// Erase unit in bytes.
    pub erase_unit: u32,
    /// Spare bytes per page (NAND), else 0.
    pub spare: u32,
}

/// The numbers entry: values `tag-info` reports.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct IdentityNumbers {
    /// `TagType` value.
    pub tag_type: u32,
    /// As `tag-info` reports it.
    pub qtmonitor_min_version: f32,
    /// As `tag-info` reports it.
    pub accel_constant: f32,
    /// As `tag-info` reports it.
    pub mag_constant: f32,
    /// Largest `TagState` value the firmware accepts.
    pub tag_state_max: u32,
    /// Largest `State_Event` value.
    pub state_event_max: u32,
}

/// A well-formed identity record.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct IdentityRecord {
    /// Record format version.
    pub format_version: u16,
    /// String entries by name: `target`, `board`, `loader`, `decoder`,
    /// `git_sha`, ...
    pub strings: BTreeMap<String, String>,
    /// The external part, when the record describes one.
    pub external_flash: Option<IdentityExternalFlash>,
    /// Regions by name: `image`, `persistent`, `state_log`,
    /// `stored_config`, `data_headers`, `calibration`, `nand_map`,
    /// `scratchpad`.
    pub regions: BTreeMap<String, IdentityRegion>,
    /// The BackupState map, when present.
    pub backup_state: Option<IdentityBackupState>,
    /// The numbers entry, when present.
    pub numbers: Option<IdentityNumbers>,
    /// Addresses of the nanopb default `Config` and of its `u32` byte
    /// length, when present.
    pub default_config: Option<(u32, u32)>,
    /// Offset of `t_sessionFacts` in the stored configuration, when present.
    pub session_facts_offset: Option<u32>,
}

impl IdentityRecord {
    /// A string field, or `""` when absent.
    pub fn string(&self, name: &str) -> &str {
        self.strings.get(name).map(String::as_str).unwrap_or("")
    }

    /// True when the record declares no external flash: no part, or a part
    /// of size 0. BitTag's record is like this.
    pub fn has_no_external_flash(&self) -> bool {
        self.external_flash.is_none_or(|x| x.flash_size == 0)
    }

    /// The `TagType` value from the numbers entry, when present.
    pub fn tag_type(&self) -> Option<u32> {
        self.numbers.map(|n| n.tag_type)
    }

    /// First byte of the persistent region (the image's
    /// `__persistent_start__`), when the record has that region entry.
    pub fn persistent_start(&self) -> Option<u32> {
        self.regions.get("persistent").map(|g| g.start)
    }
}

/// Offsets from the flash base where a record may sit, one per supported
/// MCU in [`ALL_MCUS`] order (STM32L432 `0x1A0`, then STM32U375 `0x240`).
pub fn record_offsets() -> impl Iterator<Item = u32> {
    ALL_MCUS.iter().map(|m| m.identity_offset)
}

/// Finds and parses the identity record in `flash`, internal flash read from
/// its base, whichever MCU it came from.
///
/// The [`record_offsets`] are tried in order; the first that holds the
/// `"TGID"` magic decides, so a malformed record there is an error rather
/// than a fall-through to the next offset.
///
/// # Errors
///
/// [`parse_identity_record`]'s errors, or "no tag identity record after the
/// vectors" when no offset holds the magic -- an erased part, or an image
/// built before identity records.
pub fn find_identity_record(flash: &[u8]) -> Result<IdentityRecord, String> {
    for offset in record_offsets() {
        let at = offset as usize;
        if flash.len() >= at + 4 && u32_at(flash, at) == MAGIC {
            return parse_identity_record(flash, offset);
        }
    }
    Err("no tag identity record after the vectors".into())
}

/// A little-endian `u16` at `o`; the caller has checked the range.
fn u16_at(b: &[u8], o: usize) -> u16 {
    u16::from_le_bytes([b[o], b[o + 1]])
}

/// A little-endian `u32` at `o`; the caller has checked the range.
fn u32_at(b: &[u8], o: usize) -> u32 {
    u32::from_le_bytes(b[o..o + 4].try_into().unwrap())
}

/// A little-endian `f32` at `o`; the caller has checked the range.
fn f32_at(b: &[u8], o: usize) -> f32 {
    f32::from_bits(u32_at(b, o))
}

/// Parses the record at `offset` in `flash` (bytes from the flash base).
///
/// A string value is cut at its first NUL and decoded as UTF-8, lossily.
/// When an id repeats, the last entry wins. Unknown ids, and known ids
/// shorter than their layout, are skipped, so newer records stay readable.
///
/// # Errors
///
/// "image too short for an identity record", "no identity record (magic not
/// found)" (images built before the record existed have none), "identity
/// record size out of range", "identity record entry has a bad length",
/// "identity record end entry does not match its size", or "identity record
/// has no end entry".
pub fn parse_identity_record(flash: &[u8], offset: u32) -> Result<IdentityRecord, String> {
    let base = offset as usize;
    if base + 8 > flash.len() {
        return Err("image too short for an identity record".into());
    }
    if u32_at(flash, base) != MAGIC {
        return Err("no identity record (magic not found)".into());
    }
    let mut r = IdentityRecord {
        format_version: u16_at(flash, base + 4),
        ..Default::default()
    };
    let size = u16_at(flash, base + 6) as usize;
    if size < 12 || base + size > flash.len() {
        return Err("identity record size out of range".into());
    }

    let mut off = 8;
    while off + 4 <= size {
        let id = u16_at(flash, base + off);
        let len = u16_at(flash, base + off + 2) as usize;
        off += 4;
        if id == END {
            if off != size {
                return Err("identity record end entry does not match its size".into());
            }
            return Ok(r);
        }
        if !len.is_multiple_of(4) || off + len > size {
            return Err("identity record entry has a bad length".into());
        }
        let v = base + off;
        let value = &flash[v..v + len];
        if let Some(name) = string_name(id) {
            let end = value.iter().position(|&b| b == 0).unwrap_or(len);
            r.strings
                .insert(name.into(), String::from_utf8_lossy(&value[..end]).into_owned());
        } else if let Some(name) = region_name(id).filter(|_| len >= 20) {
            r.regions.insert(
                name.into(),
                IdentityRegion {
                    start: u32_at(flash, v),
                    end: u32_at(flash, v + 4),
                    record_size: u32_at(flash, v + 8),
                    record_count: u32_at(flash, v + 12),
                    layout_version: u32_at(flash, v + 16),
                },
            );
        } else if id == NUMBERS && len >= 24 {
            r.numbers = Some(IdentityNumbers {
                tag_type: u32_at(flash, v),
                qtmonitor_min_version: f32_at(flash, v + 4),
                accel_constant: f32_at(flash, v + 8),
                mag_constant: f32_at(flash, v + 12),
                tag_state_max: u32_at(flash, v + 16),
                state_event_max: u32_at(flash, v + 20),
            });
        } else if id == BACKUP_STATE && len >= 32 {
            r.backup_state = Some(IdentityBackupState {
                base: u32_at(flash, v),
                word_count: u32_at(flash, v + 4),
                valid_magic: u32_at(flash, v + 8),
                word_valid: u32_at(flash, v + 12),
                word_state: u32_at(flash, v + 16),
                word_pages: u32_at(flash, v + 20),
                word_external_blocks: u32_at(flash, v + 24),
                word_reset_cause: u32_at(flash, v + 28),
            });
        } else if id == DEFAULT_CONFIG && len >= 8 {
            r.default_config = Some((u32_at(flash, v), u32_at(flash, v + 4)));
        } else if id == SESSION_FACTS && len >= 12 {
            r.session_facts_offset = Some(u32_at(flash, v));
        } else if id == EXTERNAL_FLASH && len >= 20 {
            r.external_flash = Some(IdentityExternalFlash {
                jedec_id: u32_at(flash, v),
                flash_size: u32_at(flash, v + 4),
                program_page: u32_at(flash, v + 8),
                erase_unit: u32_at(flash, v + 12),
                spare: u32_at(flash, v + 16),
            });
        }
        off += len;
    }
    Err("identity record has no end entry".into())
}

/// Unit tests of the parser on hand-built records.
#[cfg(test)]
mod tests {
    use super::*;

    /// A record of `entries` (id, value), format version 1, with an end
    /// entry, at offset 0x10 of an otherwise empty image.
    fn image(entries: &[(u16, Vec<u8>)]) -> Vec<u8> {
        let mut body = Vec::new();
        for (id, value) in entries {
            body.extend_from_slice(&id.to_le_bytes());
            body.extend_from_slice(&(value.len() as u16).to_le_bytes());
            body.extend_from_slice(value);
        }
        let mut f = vec![0u8; 0x10];
        f.extend_from_slice(b"TGID");
        f.extend_from_slice(&1u16.to_le_bytes());
        f.extend_from_slice(&((8 + body.len() + 4) as u16).to_le_bytes());
        f.extend_from_slice(&body);
        f.extend_from_slice(&[0xFF, 0xFF, 0, 0]);
        f
    }

    /// Words as little-endian bytes.
    fn words(w: &[u32]) -> Vec<u8> {
        w.iter().flat_map(|v| v.to_le_bytes()).collect()
    }

    /// Strings, an external part and a region come back as stored.
    #[test]
    fn parses_entries() {
        let f = image(&[
            (0x010E, b"AT25XE_PresTagv3\0\0\0\0".to_vec()),
            (EXTERNAL_FLASH, words(&[0x1F_4401, 4 << 20, 256, 4096, 0])),
            (0x0306, words(&[0x0803_F800, 0x0804_0000, 64, 0, 1])),
        ]);
        let r = parse_identity_record(&f, 0x10).unwrap();
        assert_eq!(r.string("loader"), "AT25XE_PresTagv3");
        assert_eq!(r.string("target"), "");
        assert_eq!(r.external_flash.unwrap().flash_size, 4 << 20);
        assert!(!r.has_no_external_flash());
        assert_eq!(r.regions["calibration"].start, 0x0803_F800);
    }

    /// No magic, and a size that disagrees with the end entry, are refused.
    #[test]
    fn refuses_malformed() {
        let mut f = image(&[]);
        assert!(parse_identity_record(&f, 0).unwrap_err().contains("magic"));
        let len = f.len();
        f[0x16] = 40; // declared size past the end entry
        f.resize(len + 64, 0);
        assert!(parse_identity_record(&f, 0x10).is_err());
    }

    /// The record is found at either MCU's offset in erased-filled flash,
    /// with its tag type and persistent region; erased flash has none.
    #[test]
    fn finds_record_at_either_offset() {
        let rec = image(&[
            (0x0101, b"UIUCTag\0".to_vec()),
            (NUMBERS, words(&[7, 0, 0, 0, 0, 0])),
            (0x0302, words(&[0x0800_B000, 0, 0, 0, 1])),
        ])
        .split_off(0x10);
        for offset in [0x1A0usize, 0x240] {
            let mut flash = vec![0xFFu8; offset];
            flash.extend_from_slice(&rec);
            let r = find_identity_record(&flash).unwrap();
            assert_eq!(r.string("target"), "UIUCTag");
            assert_eq!(r.tag_type(), Some(7));
            assert_eq!(r.persistent_start(), Some(0x0800_B000));
        }
        let mut flash = vec![0u8; 0x1A0];
        flash.extend_from_slice(&image(&[]).split_off(0x10));
        let r = find_identity_record(&flash).unwrap();
        assert_eq!((r.tag_type(), r.persistent_start()), (None, None));
        assert_eq!(
            find_identity_record(&[0xFFu8; 4096]).unwrap_err(),
            "no tag identity record after the vectors"
        );
    }
}
