//! What an image says it is for, read from the tag identity record ("TGID")
//! it carries after its vectors.
//!
//! The record is parsed by `tagcore::recovery::identity`, the one parser the
//! host's Rust tools share (capture, rebuild, `tag-flash`); this module only
//! finds it in an ELF and reduces it to the [`ImageIdentity`] qtprogram
//! used. The record sits at `0x08000000 + 0x1A0` (STM32L432) or `+ 0x240`
//! (STM32U375), as `tagcore::recovery::record_offsets()` lists them.
//!
//! The record is what lets the Program dialog say which target and board an
//! image was built for, and lets the record CSV note the image's commit,
//! without trusting the file name.

use serde::Serialize;
use std::path::Path;

use tagcore::recovery::{parse_identity_record, record_offsets};

use super::elf::Elf;

/// Start address of internal flash; record offsets are relative to it.
const FLASH_BASE: u32 = 0x0800_0000;

/// What an image says it is for (qtprogram's `ImageIdentity`).
///
/// The subset of the record qtprogram uses, flattened to strings; a string
/// the record lacks is empty. Serialized into the view state.
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

/// Reads the identity record from the ELF at `path`.
///
/// # Errors
///
/// As [`identity_of`], plus [`Elf::load`]'s errors.
pub fn read_image_identity(path: &Path) -> Result<ImageIdentity, String> {
    identity_of(&Elf::load(path)?)
}

/// [`read_image_identity`] for an already-loaded image.
///
/// The record's offsets are tried in order (L432 first). An offset whose
/// header is not in the image's file bytes, does not start with `"TGID"`,
/// or whose record runs past a segment is skipped; the first offset that
/// passes those checks decides, so a malformed record there is an error
/// rather than a fall-through to the next offset.
///
/// # Errors
///
/// `tagcore`'s `parse_identity_record` errors, or "no tag identity record
/// after the vectors" when no offset holds a record.
pub fn identity_of(elf: &Elf) -> Result<ImageIdentity, String> {
    for offset in record_offsets() {
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
            tag_type: record.tag_type().map_or(0, |t| t as i32),
        });
    }
    Err("no tag identity record after the vectors".into())
}

/// Builds identity records for tests.
#[cfg(test)]
pub(crate) mod testrecord {
    /// A record with `strings` (id, text) and an optional tag type.
    ///
    /// Strings are NUL-terminated and padded to 4 bytes; a tag type becomes
    /// a 24-byte numbers entry. Format version 1, with an end entry.
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

/// Unit tests of the parser and of finding the record in an image.
#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::elf::testelf;
    // The record-finding tests live with the parser, in tagcore-rs.

    /// An image whose one segment, at the flash base, holds `offset` zero
    /// bytes followed by `record`.
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
