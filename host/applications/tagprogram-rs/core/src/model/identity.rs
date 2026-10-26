//! The tag identity record ("TGID") an image carries after its vectors.
//!
//! Port of `tagcore/recovery/identityrecord.cc`'s parser, limited to what
//! qtprogram reads: the string entries and the tag type. The record sits at
//! `0x08000000 + 0x1A0` (STM32L432 vector table) or `+ 0x240` (STM32U375).
//! It is a header (`"TGID"`, format version, total size; all little-endian)
//! followed by `id`, `len`, value entries and an end entry (`0xFFFF`).

use serde::Serialize;
use std::collections::HashMap;
use std::path::Path;

use super::elf::Elf;

const FLASH_BASE: u32 = 0x0800_0000;
/// Offsets from the flash base where an image may carry its record.
const RECORD_OFFSETS: [u32; 2] = [0x1A0, 0x240];
const MAGIC: u32 = 0x4449_4754; // "TGID"
const END: u16 = 0xFFFF;
const NUMBERS: u16 = 0x0201;

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

/// What an image says it is for (qtprogram's `ImageIdentity`).
#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize)]
pub struct ImageIdentity {
    /// The image carries a well-formed identity record.
    pub found: bool,
    /// Build target, e.g. `"BitTag"`; names a release directory.
    pub target: String,
    /// Board directory, e.g. `"BitTagv6"`.
    pub board: String,
    /// `BOARD_NAME`, as `TagInfo.board_desc` reports it.
    pub board_desc: String,
    /// Short commit, as `TagInfo.githash` reports it.
    pub git_hash: String,
    /// Full commit.
    pub git_sha: String,
    /// e.g. `"/embedded/tags/BitTag"`, as `TagInfo.source_path`.
    pub source_path: String,
    /// `TagType` value (`proto/tagdata.proto`); 0 when the record has no
    /// numbers entry.
    pub tag_type: i32,
}

/// A parsed record: its strings by name and, if present, the tag type.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct IdentityRecord {
    /// Record format version from the header.
    pub format_version: u16,
    /// String entries by name (`"target"`, `"git_hash"`, ...).
    pub strings: HashMap<String, String>,
    /// `TagType` from the numbers entry, when the record has one.
    pub tag_type: Option<u32>,
}

impl IdentityRecord {
    /// The string entry `name`, or `""`.
    pub fn string(&self, name: &str) -> &str {
        self.strings.get(name).map(String::as_str).unwrap_or("")
    }
}

fn u16_at(b: &[u8], o: usize) -> u16 {
    u16::from_le_bytes([b[o], b[o + 1]])
}

/// Parses the record at `offset` in `flash` (flash bytes from the base).
///
/// Errors are `ParseIdentityRecord`'s: too short, no magic, size out of
/// range, a bad entry length, a misplaced end entry, or no end entry.
pub fn parse_identity_record(flash: &[u8], offset: u32) -> Result<IdentityRecord, String> {
    let base = offset as usize;
    if base + 8 > flash.len() {
        return Err("image too short for an identity record".into());
    }
    if u32::from_le_bytes(flash[base..base + 4].try_into().unwrap()) != MAGIC {
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
        let value = &flash[base + off..base + off + len];
        if let Some(name) = string_name(id) {
            let end = value.iter().position(|&b| b == 0).unwrap_or(len);
            r.strings.insert(
                name.into(),
                String::from_utf8_lossy(&value[..end]).into_owned(),
            );
        } else if id == NUMBERS && len >= 24 {
            r.tag_type = Some(u32::from_le_bytes(value[0..4].try_into().unwrap()));
        }
        off += len;
    }
    Err("identity record has no end entry".into())
}

/// Reads the identity record from the ELF at `path`.
///
/// Tries each record offset whose 8 header bytes lie in one loaded segment
/// and start with `"TGID"`; the first such record is parsed and its result
/// returned. Errors: the ELF's own load error, the record's parse error, or
/// "no tag identity record after the vectors".
pub fn read_image_identity(path: &Path) -> Result<ImageIdentity, String> {
    identity_of(&Elf::load(path)?)
}

/// [`read_image_identity`] for an already-loaded image.
pub fn identity_of(elf: &Elf) -> Result<ImageIdentity, String> {
    for offset in RECORD_OFFSETS {
        let Some(header) = elf.read_at(FLASH_BASE + offset, 8) else {
            continue;
        };
        if &header[0..4] != b"TGID" {
            continue;
        }
        let size = u32::from(header[6]) | u32::from(header[7]) << 8;
        let Some(flash) = elf.read_at(FLASH_BASE, offset + size) else {
            continue;
        };
        let record = parse_identity_record(flash, offset)?;
        let text = |name: &str| record.string(name).to_string();
        return Ok(ImageIdentity {
            found: true,
            target: text("target"),
            board: text("board"),
            board_desc: text("board_desc"),
            git_hash: text("git_hash"),
            git_sha: text("git_sha"),
            source_path: text("source_path"),
            tag_type: record.tag_type.map_or(0, |t| t as i32),
        });
    }
    Err("no tag identity record after the vectors".into())
}

/// Builds identity records for tests.
#[cfg(test)]
pub(crate) mod testrecord {
    /// A record with `strings` (id, text) and an optional tag type.
    pub fn build(strings: &[(u16, &str)], tag_type: Option<u32>) -> Vec<u8> {
        let mut body = Vec::new();
        let mut entry = |id: u16, mut value: Vec<u8>| {
            value.resize(value.len().div_ceil(4) * 4, 0);
            body.extend_from_slice(&id.to_le_bytes());
            body.extend_from_slice(&(value.len() as u16).to_le_bytes());
            body.extend_from_slice(&value);
        };
        for (id, text) in strings {
            let mut v = text.as_bytes().to_vec();
            v.push(0);
            entry(*id, v);
        }
        if let Some(t) = tag_type {
            let mut v = t.to_le_bytes().to_vec();
            v.resize(24, 0);
            entry(0x0201, v);
        }
        let size = (8 + body.len() + 4) as u16;
        let mut r = b"TGID".to_vec();
        r.extend_from_slice(&1u16.to_le_bytes());
        r.extend_from_slice(&size.to_le_bytes());
        r.extend_from_slice(&body);
        r.extend_from_slice(&[0xFF, 0xFF, 0, 0]);
        r
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::elf::testelf;

    fn image(offset: usize, record: &[u8]) -> Elf {
        let mut flash = vec![0u8; offset];
        flash.extend_from_slice(record);
        let len = flash.len() as u32;
        let bytes = testelf::build(&[(FLASH_BASE, flash, len)], &[]);
        Elf::parse(&bytes, "t.elf").unwrap()
    }

    #[test]
    fn reads_strings_and_tag_type_at_either_offset() {
        let rec = testrecord::build(&[(0x0101, "BitTag"), (0x0108, "56e5e6a0")], Some(3));
        for offset in [0x1A0, 0x240] {
            let id = identity_of(&image(offset, &rec)).unwrap();
            assert!(id.found);
            assert_eq!(id.target, "BitTag");
            assert_eq!(id.git_hash, "56e5e6a0");
            assert_eq!(id.board, "");
            assert_eq!(id.tag_type, 3);
        }
        let id = identity_of(&image(0x1A0, &testrecord::build(&[], None))).unwrap();
        assert_eq!(id.tag_type, 0);
    }

    #[test]
    fn errors() {
        assert_eq!(
            identity_of(&image(0x300, b"")).unwrap_err(),
            "no tag identity record after the vectors"
        );
        let mut rec = testrecord::build(&[(0x0101, "BitTag")], None);
        let n = rec.len();
        rec[n - 4] = 0; // the end entry becomes an unknown entry
        assert_eq!(
            identity_of(&image(0x1A0, &rec)).unwrap_err(),
            "identity record has no end entry"
        );
        assert_eq!(
            parse_identity_record(b"TGI", 0).unwrap_err(),
            "image too short for an identity record"
        );
        let mut rec = testrecord::build(&[], None);
        rec[6] = 8;
        assert_eq!(
            parse_identity_record(&rec, 0).unwrap_err(),
            "identity record size out of range"
        );
    }
}
