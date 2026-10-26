//! A firmware image: hashed, planned, identified and checked against its
//! build manifest.
//!
//! Port of qtprogram's `firmwareimage.cpp`. An image is the `.elf` itself;
//! beside it a release carries `<stem>-build-manifest.json`, whose
//! `artifacts.<file>.sha256` and `.bytes` must match before it is programmed.
//!
//! The manifest check is what guarantees that the bytes programmed into a
//! tag are the bytes that were archived for the release: a release image
//! without a manifest, or one whose hash or size differs, is refused. A file
//! chosen by hand needs no manifest, but one found beside it is still
//! checked. The module also provides the read-back helpers Retest uses
//! ([`image_flash_range`], [`compare_image_flash`]).

use serde::Serialize;
use sha2::{Digest, Sha256};
use std::path::{Path, PathBuf};

use super::elf::Elf;
use super::flashplan::{Erase, FlashPlan, plan_flash};
use super::identity::{ImageIdentity, read_image_identity};

/// Start address of internal flash; segments below it (RAM, NOLOAD) are not
/// programmed and are ignored by the read-back helpers.
const FLASH_BASE: u32 = 0x0800_0000;

/// One image, as loaded for programming (qtprogram's `FirmwareImage`).
///
/// Built by [`load_firmware_image`], which fills the fields in order and
/// stops at the first refusal, so fields after the failing step keep their
/// defaults. Serialized into the view state.
#[derive(Debug, Clone, Default, Serialize)]
pub struct FirmwareImage {
    /// Path of the `.elf` file; empty in the default (no image) value.
    pub elf_path: PathBuf,
    /// Release tag, e.g. `"fw-v0.7"`; `None` for a file chosen by hand.
    pub release: Option<String>,
    /// `<stem>-build-manifest.json` beside the image, when there is one.
    pub manifest_path: Option<PathBuf>,
    /// SHA-256 of the ELF, lowercase hex; empty if it could not be read.
    pub sha256: String,
    /// Size of the ELF in bytes.
    pub bytes: u64,
    /// The hash and size match the manifest's record.
    pub manifest_verified: bool,
    /// The manifest's `source.commit`, or empty when unverified or absent.
    pub manifest_commit: String,
    /// Why the image must not be programmed; empty when usable.
    pub error: String,
    /// A concern to show that does not prevent programming, e.g. a manifest
    /// whose `source.dirty` is true.
    pub warning: String,
    /// What the image says it is for.
    pub identity: ImageIdentity,
    /// The internal-flash erase that goes with it (see [`plan_flash`]).
    pub plan: FlashPlan,
}

impl FirmwareImage {
    /// The image may be programmed: one was loaded and nothing refused it.
    pub fn usable(&self) -> bool {
        !self.elf_path.as_os_str().is_empty() && self.error.is_empty()
    }

    /// The record's `firmware_version`: the release tag, or `"custom"`.
    pub fn version(&self) -> &str {
        self.release.as_deref().unwrap_or("custom")
    }

    /// `"BitTag (fw-v0.7)"`, or `"BitTag.elf (custom)"` for an image without
    /// an identity record.
    pub fn display_name(&self) -> String {
        let name = if self.identity.found {
            self.identity.target.clone()
        } else {
            file_name(&self.elf_path)
        };
        format!("{name} ({})", self.version())
    }
}

/// The final component of `path`, lossily as UTF-8, or `""` when it has
/// none.
fn file_name(path: &Path) -> String {
    path.file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default()
}

/// Lowercase hex SHA-256 of the file at `path`, or `""` if it cannot be read.
///
/// The file is streamed, not read into memory. The result is 64 characters
/// on success, the same text the build manifest records.
pub fn sha256_of_file(path: &Path) -> String {
    let Ok(mut file) = std::fs::File::open(path) else {
        return String::new();
    };
    let mut hash = Sha256::new();
    if std::io::copy(&mut file, &mut hash).is_err() {
        return String::new();
    }
    hash.finalize().iter().map(|b| format!("{b:02x}")).collect()
}

/// Two commit names refer to the same commit: both are non-empty and one is
/// a prefix of the other, ignoring case (a short hash against a full SHA).
///
/// # Examples
///
/// ```
/// use tagprogram_core::model::image::same_commit;
///
/// assert!(same_commit("56E5E6A0", "56e5e6a0abcdef"));
/// assert!(!same_commit("", "56e5e6a0"));
/// ```
pub fn same_commit(a: &str, b: &str) -> bool {
    if a.is_empty() || b.is_empty() {
        return false;
    }
    let (a, b) = (a.to_ascii_lowercase(), b.to_ascii_lowercase());
    a.starts_with(&b) || b.starts_with(&a)
}

/// Checks `image`'s hash and size against the manifest at `manifest_path`.
///
/// On success sets `manifest_verified` and `manifest_commit`, and a warning
/// when the manifest says the tree was dirty. Otherwise sets `error` to the
/// first failure: the manifest cannot be read; it has no non-empty
/// `artifacts.<file name>` object (also the result for a manifest that is not
/// JSON); the SHA-256 differs; or `bytes` differs (a missing `bytes` counts
/// as -1, so it always differs).
fn verify_manifest(image: &mut FirmwareImage, manifest_path: &Path) {
    let Ok(text) = std::fs::read(manifest_path) else {
        image.error = format!("Cannot read {}.", manifest_path.display());
        return;
    };
    let manifest: serde_json::Value = serde_json::from_slice(&text).unwrap_or_default();
    let name = file_name(&image.elf_path);
    let recorded = &manifest["artifacts"][&name];
    if recorded.as_object().is_none_or(|o| o.is_empty()) {
        image.error = format!("{} records no hash for {name}.", file_name(manifest_path));
        return;
    }
    let sha = recorded["sha256"].as_str().unwrap_or("");
    if sha != image.sha256 {
        image.error = format!(
            "{name} does not match its manifest (recorded {sha}, actual {}). \
             This is not the image that was archived; do not program it.",
            image.sha256
        );
        return;
    }
    let bytes = recorded["bytes"].as_i64().unwrap_or(-1);
    if bytes != image.bytes as i64 {
        image.error = format!(
            "{name} is {} bytes; its manifest says {bytes}.",
            image.bytes
        );
        return;
    }
    image.manifest_verified = true;
    image.manifest_commit = manifest["source"]["commit"]
        .as_str()
        .unwrap_or("")
        .to_string();
    if manifest["source"]["dirty"].as_bool().unwrap_or(false) {
        image.warning = "Built from a tree with uncommitted changes, so its commit does not \
                         describe its sources."
            .into();
    }
}

/// Loads the image at `elf_path`; `release` is its release tag, if any.
///
/// Never fails outright: the returned image's `error` says why it must not
/// be programmed (missing or unreadable file, no flash plan, a manifest that
/// does not match, or, for a release, no manifest at all). An image without
/// an identity record is still usable.
///
/// Steps, each stopping at its failure: the file exists and is a regular
/// file; it is hashed; it has a valid [`FlashPlan`]; its identity record is
/// read (a missing or malformed one leaves the default identity); then the
/// manifest is checked if present. An empty `release` counts as none.
pub fn load_firmware_image(elf_path: &Path, release: Option<&str>) -> FirmwareImage {
    let mut image = FirmwareImage {
        elf_path: elf_path.to_path_buf(),
        release: release.filter(|r| !r.is_empty()).map(str::to_string),
        ..Default::default()
    };
    let p = elf_path.display();
    let meta = match std::fs::metadata(elf_path) {
        Ok(m) if m.is_file() => m,
        _ => {
            image.error = format!("{p} does not exist.");
            return image;
        }
    };
    image.bytes = meta.len();
    image.sha256 = sha256_of_file(elf_path);
    if image.sha256.is_empty() {
        image.error = format!("Cannot read {p}.");
        return image;
    }
    image.plan = plan_flash(elf_path);
    if image.plan.erase == Erase::Invalid {
        image.error = image.plan.error.clone();
        return image;
    }
    image.identity = read_image_identity(elf_path).unwrap_or_default();

    let stem = elf_path
        .file_stem()
        .map(|s| s.to_string_lossy())
        .unwrap_or_default();
    let manifest = elf_path.with_file_name(format!("{stem}-build-manifest.json"));
    if manifest.exists() {
        verify_manifest(&mut image, &manifest);
        image.manifest_path = Some(manifest);
    } else if image.release.is_some() {
        image.error = format!(
            "The release has no {} beside {}, so the image cannot be checked.",
            file_name(&manifest),
            file_name(elf_path)
        );
    }
    image
}

/// The internal-flash bytes the image at `elf_path` loads: `(start, len)`
/// from its lowest to the end of its highest loaded byte at or above
/// `0x08000000`.
///
/// Only segments with file bytes count; a gap between segments is included
/// in the range. Retest reads this range back from the tag and passes it to
/// [`compare_image_flash`].
///
/// # Errors
///
/// The ELF's load error, or "... loads nothing into internal flash."
pub fn image_flash_range(elf_path: &Path) -> Result<(u32, u32), String> {
    let elf = Elf::load(elf_path)?;
    let (mut lo, mut hi) = (u64::MAX, 0u64);
    for s in elf
        .segments
        .iter()
        .filter(|s| s.addr >= FLASH_BASE && !s.data.is_empty())
    {
        lo = lo.min(u64::from(s.addr));
        hi = hi.max(u64::from(s.addr) + s.data.len() as u64);
    }
    if hi == 0 {
        return Err(format!(
            "{} loads nothing into internal flash.",
            elf_path.display()
        ));
    }
    Ok((lo as u32, (hi - lo) as u32))
}

/// Compares `flash`, read from the tag at `start`, with the image at
/// `elf_path`.
///
/// Returns `None` when every internal-flash byte the image loads matches;
/// otherwise why not: the ELF's load error, a read that does not cover the
/// image, or the first differing address.
///
/// `start` is the address of `flash[0]`. Only bytes the image loads are
/// compared, so gaps between segments and the rest of flash may hold
/// anything. Segments are compared in program-header order and the first
/// mismatch is reported.
pub fn compare_image_flash(elf_path: &Path, start: u32, flash: &[u8]) -> Option<String> {
    let elf = match Elf::load(elf_path) {
        Ok(e) => e,
        Err(why) => return Some(why),
    };
    let end = u64::from(start) + flash.len() as u64;
    for s in elf
        .segments
        .iter()
        .filter(|s| s.addr >= FLASH_BASE && !s.data.is_empty())
    {
        if s.addr < start || u64::from(s.addr) + s.data.len() as u64 > end {
            return Some(format!(
                "Only 0x{start:08x}-0x{:08x} was read, which does not cover the image.",
                end as u32
            ));
        }
        let read = &flash[(s.addr - start) as usize..];
        if let Some(i) = s.data.iter().zip(read).position(|(a, b)| a != b) {
            return Some(format!(
                "The tag's flash differs from the image at 0x{:08x}.",
                s.addr + i as u32
            ));
        }
    }
    None
}

/// Unit tests of loading, manifest checking and the read-back helpers, on
/// synthetic images in temporary directories.
#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::elf::testelf;
    use crate::model::identity::testrecord;

    /// A U375-style image with a BitTag identity record at 0x1A0.
    fn write_image(dir: &Path, name: &str) -> PathBuf {
        let mut flash = vec![0xAAu8; 0x1A0];
        flash.extend(testrecord::build(&[(0x0101, "BitTag")], Some(1)));
        let len = flash.len() as u32;
        let bytes = testelf::build(
            &[(FLASH_BASE, flash, len), (0x2000_0000, vec![], 0x100)],
            &[("__tag_flash_page_size__", 4096)],
        );
        let path = dir.join(name);
        std::fs::write(&path, bytes).unwrap();
        path
    }

    /// Writes `<stem>-build-manifest.json` beside `elf`, recording `sha` and
    /// `bytes` for it and a source commit with the given dirty flag.
    fn write_manifest(elf: &Path, sha: &str, bytes: u64, dirty: bool) {
        let name = elf.file_name().unwrap().to_str().unwrap();
        let stem = elf.file_stem().unwrap().to_str().unwrap();
        let m = serde_json::json!({
            "artifacts": { name: { "sha256": sha, "bytes": bytes } },
            "source": { "commit": "56e5e6a0abc", "dirty": dirty },
        });
        std::fs::write(
            elf.with_file_name(format!("{stem}-build-manifest.json")),
            m.to_string(),
        )
        .unwrap();
    }

    #[test]
    fn custom_image_without_manifest_is_usable() {
        let dir = tempfile::tempdir().unwrap();
        let elf = write_image(dir.path(), "BitTag.elf");
        let img = load_firmware_image(&elf, None);
        assert!(img.usable(), "{}", img.error);
        assert_eq!(img.display_name(), "BitTag (custom)");
        assert_eq!(img.identity.tag_type, 1);
        assert_eq!(img.sha256.len(), 64);
        assert!(!img.manifest_verified);
    }

    #[test]
    fn release_image_checks_its_manifest() {
        let dir = tempfile::tempdir().unwrap();
        let elf = write_image(dir.path(), "BitTag.elf");
        let img = load_firmware_image(&elf, Some("fw-v0.6"));
        assert_eq!(
            img.error,
            "The release has no BitTag-build-manifest.json beside BitTag.elf, so the image cannot be checked."
        );

        let sha = sha256_of_file(&elf);
        let size = std::fs::metadata(&elf).unwrap().len();
        write_manifest(&elf, &sha, size, true);
        let img = load_firmware_image(&elf, Some("fw-v0.6"));
        assert!(img.usable() && img.manifest_verified);
        assert_eq!(img.manifest_commit, "56e5e6a0abc");
        assert!(
            img.warning
                .starts_with("Built from a tree with uncommitted changes")
        );
        assert_eq!(img.display_name(), "BitTag (fw-v0.6)");

        write_manifest(&elf, &sha, size + 1, false);
        let img = load_firmware_image(&elf, Some("fw-v0.6"));
        assert_eq!(
            img.error,
            format!(
                "BitTag.elf is {size} bytes; its manifest says {}.",
                size + 1
            )
        );

        write_manifest(&elf, "00", size, false);
        assert!(
            load_firmware_image(&elf, None)
                .error
                .starts_with("BitTag.elf does not match its manifest (recorded 00, actual ")
        );
    }

    #[test]
    fn missing_file() {
        let img = load_firmware_image(Path::new("/nonexistent/x.elf"), None);
        assert_eq!(img.error, "/nonexistent/x.elf does not exist.");
        assert!(!img.usable());
        assert_eq!(img.display_name(), "x.elf (custom)");
    }

    #[test]
    fn flash_range_and_compare() {
        let dir = tempfile::tempdir().unwrap();
        let elf = write_image(dir.path(), "BitTag.elf");
        let (start, len) = image_flash_range(&elf).unwrap();
        assert_eq!(start, FLASH_BASE);
        let e = Elf::load(&elf).unwrap();
        let mut flash = e.segments[0].data.clone();
        assert_eq!(len as usize, flash.len());
        assert_eq!(compare_image_flash(&elf, start, &flash), None);
        flash[5] ^= 1;
        assert_eq!(
            compare_image_flash(&elf, start, &flash).unwrap(),
            "The tag's flash differs from the image at 0x08000005."
        );
        assert_eq!(
            compare_image_flash(&elf, start, &flash[..10]).unwrap(),
            "Only 0x08000000-0x0800000a was read, which does not cover the image."
        );
    }

    #[test]
    fn commits() {
        assert!(same_commit("56E5E6A0", "56e5e6a0abcdef"));
        assert!(!same_commit("", "56e5"));
        assert!(!same_commit("56e5e6a1", "56e5e6a0abcdef"));
    }

    /// The published fw-v0.6 images, when a copy has been extracted locally.
    ///
    /// Skipped (and passes) when the directory is absent.
    // TODO(doc-review): the path is a past session's scratch directory, so on
    // any other machine or session this test never runs.
    #[test]
    fn real_release_images() {
        let root = Path::new(
            "/private/tmp/claude-503/-Users-geobrown-Research-tag-designs-software/\
             2fbf841b-360b-441c-a006-67aa9e78031d/scratchpad/fw-v0.6/x/firmware",
        );
        if !root.exists() {
            eprintln!("skipped: {} absent", root.display());
            return;
        }
        let bit = load_firmware_image(&root.join("BitTag/BitTag.elf"), Some("fw-v0.6"));
        assert!(bit.usable(), "{}", bit.error);
        assert!(bit.manifest_verified);
        assert_eq!(bit.plan.erase, Erase::All);
        assert!(bit.plan.no_layout);
        assert_eq!(
            bit.plan.summary(),
            "Mass erase and program (STM32L432 image built before images recorded their flash layout)"
        );
        assert_eq!(bit.identity.target, "BitTag");
        assert!(image_flash_range(&bit.elf_path).is_ok());

        let imu = load_firmware_image(
            &root.join("IMUTagNandBmp581/IMUTagNandBmp581.elf"),
            Some("fw-v0.6"),
        );
        assert!(imu.usable(), "{}", imu.error);
        assert!(imu.manifest_verified);
        assert_eq!(imu.plan.erase, Erase::None);
        assert_eq!(imu.identity.target, "IMUTagNandBmp581");
    }
}
