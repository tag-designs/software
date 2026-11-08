//! The rescue erase: blank a tag's external flash so that it agrees with an
//! internal flash about to be erased.
//!
//! A rescue is three steps, each its own session, run and watched one at a
//! time (`docs/bench/capturing-a-tag.md`, "Rescuing a tag"):
//!
//! 1. this erase of the external flash, proved blank;
//! 2. an internal erase and reflash (`tag-flash`);
//! 3. `tag-reset` in a monitor session.
//!
//! The order is fixed (`embedded/loaders/design/loader-runtime.md`, "Rescue
//! erase"). A tag whose internal flash is blank but whose external flash
//! still holds data programs new pages over old ones without any error, so
//! the external flash goes first; an interruption after this step leaves an
//! internal index over blank pages, which `tag-reset` repairs.
//!
//! [`rescue_erase`] reads every sector and erases only those that are not
//! blank, then reads each erased sector back. That proves the whole part
//! blank from the host's side for the cost of one full read plus the used
//! sectors twice. It refuses:
//!
//! - unless it is given a capture of the same tag, matched by the chip's
//!   96-bit UID, or told explicitly that there is none, so that no tag is
//!   erased before its evidence is kept;
//! - a read-only loader image (use the `-RW` build);
//! - a paged part (SPI NAND): no loader can erase one yet, and a NAND rescue
//!   must also leave factory-marked bad blocks alone;
//! - a part whose JEDEC ID differs from the one the image's identity record
//!   names, when the image has a record;
//! - an image whose identity record names no external flash at all.

use std::path::Path;
use std::time::Instant;

use super::identity::find_identity_record;
use super::image::TargetImage;
use super::session::Session;
use super::xflash::ExternalFlash;

/// Bytes of internal flash read to find the identity record; it sits at
/// 0x1A0 (STM32L432) or 0x240 (STM32U375) and is well under 1 KB.
const IDENTITY_SPAN: usize = 4096;

/// Which capture, if any, vouches that the tag's evidence was kept.
#[derive(Debug, Clone, Copy)]
pub enum CaptureCheck<'a> {
    /// A `tag-capture` directory, whose manifest's `target.uid` must equal
    /// the attached tag's UID.
    Capture(&'a Path),
    /// The caller states that no capture is wanted.
    NoCapture,
}

/// Progress through the erase, reported after every sector.
#[derive(Debug, Clone, Copy)]
pub struct RescueProgress {
    /// Bytes of the part scanned so far.
    pub done: u32,
    /// The part's size in bytes.
    pub total: u32,
    /// Sectors erased so far.
    pub erased: u32,
}

/// What a rescue erase did.
#[derive(Debug, Clone, Default)]
pub struct RescueReport {
    /// The tag's 96-bit UID, in the monitor's `info.uuid` form.
    pub uid: String,
    /// The JEDEC ID the loader read (manufacturer, type, capacity).
    pub jedec: u32,
    /// The status register as the loader found it; the protect bits are
    /// evidence.
    pub sr1: u32,
    /// Part size in bytes.
    pub size: u32,
    /// Erase unit in bytes.
    pub sector_size: u32,
    /// Sectors read.
    pub sectors_scanned: u32,
    /// Sectors that held data and were erased, then read back blank.
    pub sectors_erased: u32,
    /// Notes worth printing: no identity record, no JEDEC check, and so on.
    pub notes: Vec<String>,
    /// Wall time in seconds.
    pub seconds: f64,
}

/// Reads the attached tag's 96-bit UID, in the monitor's `info.uuid` form
/// (the highest word first, as `tag-capture` writes it).
///
/// # Errors
///
/// No session, an unknown MCU, or an SWD read that failed.
pub fn read_uid(s: &mut Session) -> Result<String, String> {
    let m = s.mcu().ok_or("no recovery session")?;
    let addr = m.uid;
    let mut w = [0u32; 3];
    for (i, word) in w.iter_mut().enumerate() {
        *word = s
            .read_word(addr + 4 * i as u32)
            .ok_or("could not read the chip UID")?;
    }
    Ok(format!("{:08X}{:08X}{:08X}", w[2], w[1], w[0]))
}

/// The chip UID a capture directory's manifest records (`target.uid`).
///
/// # Errors
///
/// The manifest cannot be read or parsed, or names no UID.
pub fn capture_uid(dir: &Path) -> Result<String, String> {
    let path = dir.join("manifest.json");
    let text = std::fs::read_to_string(&path)
        .map_err(|e| format!("cannot read {}: {e}", path.display()))?;
    let v: serde_json::Value =
        serde_json::from_str(&text).map_err(|e| format!("{}: {e}", path.display()))?;
    v.get("target")
        .and_then(|t| t.get("uid"))
        .and_then(|u| u.as_str())
        .filter(|u| !u.is_empty())
        .map(str::to_string)
        .ok_or_else(|| format!("{} records no target.uid", path.display()))
}

/// Whether the JEDEC ID a loader reads (`manufacturer << 16 | type << 8 |
/// capacity`) is the part an identity record names (`manufacturer << 16 |
/// device1`, the two bytes the firmware knows).
pub fn jedec_matches(loader_jedec: u32, record_jedec: u32) -> bool {
    (loader_jedec >> 16) & 0xFF == (record_jedec >> 16) & 0xFF
        && (loader_jedec >> 8) & 0xFF == record_jedec & 0xFF
}

/// Erases the attached tag's external flash and proves it blank, through a
/// writable loader's `Serve()`. The session must be open (the core halted at
/// its reset vector); it is left open, with `Serve()` exited.
///
/// `progress` is called after every sector.
///
/// # Errors
///
/// Any refusal listed in the module documentation; the loader failing to
/// start; a read, erase or blank check that failed, naming the offset. An
/// error after the first erase leaves the part partly erased, which is safe:
/// the next step, the internal erase, has not run.
pub fn rescue_erase(
    s: &mut Session,
    loader: &TargetImage,
    check: CaptureCheck,
    mut progress: Option<&mut dyn FnMut(&RescueProgress)>,
) -> Result<RescueReport, String> {
    let t0 = Instant::now();
    let mut r = RescueReport {
        uid: read_uid(s)?,
        ..Default::default()
    };

    match check {
        CaptureCheck::Capture(dir) => {
            let captured = capture_uid(dir)?;
            if !captured.eq_ignore_ascii_case(&r.uid) {
                return Err(format!(
                    "the capture is of tag {captured}, but tag {} is attached; refusing",
                    r.uid
                ));
            }
        }
        CaptureCheck::NoCapture => r.notes.push("no capture kept (--no-capture)".into()),
    }

    // The identity record, if the internal flash still holds one, names the
    // part. A tag being rescued may have none.
    let flash_base = s.mcu().ok_or("no recovery session")?.flash_base;
    let record = s
        .read_vec(flash_base, IDENTITY_SPAN, 4096)
        .ok()
        .and_then(|head| find_identity_record(&head).ok());
    let expected = record.as_ref().and_then(|rec| rec.external_flash);
    match &expected {
        Some(x) if x.spare != 0 => {
            return Err(
                "the image's external flash is paged (SPI NAND): no loader can erase it yet".into(),
            );
        }
        Some(_) => {}
        None if record.is_some() => {
            return Err("the image's identity record names no external flash".into());
        }
        None => r.notes.push(
            "no identity record in internal flash: the part is not checked against the image"
                .into(),
        ),
    }

    let mut xf = ExternalFlash::open(s, loader).map_err(|e| format!("loader failed: {e}"))?;
    let result = erase_all(
        s,
        &mut xf,
        expected.map(|x| x.jedec_id),
        &mut r,
        &mut progress,
    );
    let closed = xf.close(s);
    result?;
    closed.map_err(|e| format!("Serve() did not exit cleanly: {e}"))?;
    r.seconds = t0.elapsed().as_secs_f64();
    Ok(r)
}

/// The sector loop of [`rescue_erase`], with the loader running.
fn erase_all(
    s: &mut Session,
    xf: &mut ExternalFlash,
    expected_jedec: Option<u32>,
    r: &mut RescueReport,
    progress: &mut Option<&mut dyn FnMut(&RescueProgress)>,
) -> Result<(), String> {
    r.jedec = xf.jedec();
    r.sr1 = xf.sr1();
    r.size = xf.size();
    r.sector_size = xf.sector_size();
    if !xf.writable() {
        return Err("the loader image is read-only; use its -RW build".into());
    }
    if xf.page_bytes() != 0 {
        return Err("the loader's part is paged (SPI NAND): rescue erase is NOR only".into());
    }
    if let Some(want) = expected_jedec
        && !jedec_matches(r.jedec, want)
    {
        return Err(format!(
            "the loader reads JEDEC 0x{:06X}, but the image names 0x{want:06X}; refusing",
            r.jedec
        ));
    }
    if r.sector_size == 0 || r.size == 0 || !r.size.is_multiple_of(r.sector_size) {
        return Err(format!(
            "the loader reports a {}-byte part in {}-byte sectors",
            r.size, r.sector_size
        ));
    }

    let mut buf = vec![0u8; r.sector_size as usize];
    let mut offset = 0u32;
    while offset < r.size {
        xf.read(s, offset, &mut buf, None)
            .map_err(|e| format!("read at 0x{offset:06X}: {e}"))?;
        r.sectors_scanned += 1;
        if buf.iter().any(|&b| b != 0xFF) {
            xf.erase_sector(s, offset)
                .map_err(|e| format!("erase at 0x{offset:06X}: {e}"))?;
            xf.read(s, offset, &mut buf, None)
                .map_err(|e| format!("blank check at 0x{offset:06X}: {e}"))?;
            if let Some(i) = buf.iter().position(|&b| b != 0xFF) {
                return Err(format!(
                    "sector at 0x{offset:06X} is not blank after its erase (byte 0x{:06X} reads 0x{:02X})",
                    offset as usize + i,
                    buf[i]
                ));
            }
            r.sectors_erased += 1;
        }
        offset += r.sector_size;
        if let Some(p) = progress.as_deref_mut() {
            p(&RescueProgress {
                done: offset,
                total: r.size,
                erased: r.sectors_erased,
            });
        }
    }
    Ok(())
}

/// Unit tests of the checks that need no tag.
#[cfg(test)]
mod tests {
    use super::*;

    /// The record keeps manufacturer and device1; the loader reads all three.
    #[test]
    fn jedec_comparison() {
        assert!(jedec_matches(0x1F_47_08, 0x1F_0047));
        assert!(!jedec_matches(0x1F_46_08, 0x1F_0047));
        assert!(!jedec_matches(0xC8_47_08, 0x1F_0047));
    }

    /// The UID comes from the manifest's target block.
    #[test]
    fn reads_capture_uid() {
        let dir = std::env::temp_dir().join(format!("rescue-uid-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(
            dir.join("manifest.json"),
            r#"{"format": "tag-capture", "target": {"mcu": "STM32L432", "uid": "20333050364150040063005F"}}"#,
        )
        .unwrap();
        assert_eq!(capture_uid(&dir).unwrap(), "20333050364150040063005F");
        std::fs::write(dir.join("manifest.json"), r#"{"target": {}}"#).unwrap();
        assert!(capture_uid(&dir).is_err());
        let _ = std::fs::remove_dir_all(&dir);
    }
}
