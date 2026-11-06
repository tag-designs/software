//! The firmware build stored with a capture (`tag-capture --with-build`).
//!
//! A capture decodes with confidence only against the image that produced
//! it: test images carry `UDEFS` that leave no trace in the git hash, so the
//! identity record alone does not identify them. This stores that image and
//! its companions under `build/` in the capture directory, records their
//! hashes and the image's region symbols in the manifest, and checks the
//! image against the internal flash just captured, so the capture says
//! whether the stored ELF is what was running.
//!
//! [`BuildFiles::resolve`] checks the options before anything attaches;
//! [`BuildFiles::store`] copies and checks once the capture is written.
//!
//! From a build directory (`embedded/tags/<Target>/build`) it stores the one
//! `*.elf` there and, when present beside it, `<stem>.map`, `<stem>.list` and
//! `<stem>-build-manifest.json` (commit, dirty flag, submodules, toolchain).
//! The `.bin`, `.hex`, `.dmp`, `obj/` and `lst/` outputs are not stored: the
//! ELF holds everything they do.

use std::fmt::Write as _;
use std::path::{Path, PathBuf};

use super::external::sha256_hex;
use super::image::TargetImage;

/// Linker symbols recorded from the image: the region bounds a decoder of
/// the capture needs (as `tag_capture_state.py` recorded them).
const REGION_SYMBOLS: [&str; 4] = [
    "__tag_code_limit__",
    "__tag_config_start__",
    "__tag_nand_map_start__",
    "__flash0_end__",
];

/// Subdirectory of the capture that receives the files.
pub const BUILD_SUBDIR: &str = "build";

/// What to store, checked before the capture starts.
#[derive(Debug, Clone)]
pub struct BuildFiles {
    /// The build directory named, or the ELF's directory.
    pub source_dir: PathBuf,
    /// The firmware ELF.
    pub elf: PathBuf,
    /// Every file to store, the ELF first, then its companions, then the
    /// extra files in the order given.
    pub files: Vec<PathBuf>,
}

/// One stored file.
#[derive(Debug, Clone)]
pub struct StoredFile {
    /// Path relative to the capture directory (`build/NAME`).
    pub file: String,
    /// Where it was copied from, absolute when it could be resolved.
    pub source: PathBuf,
    /// Size in bytes.
    pub bytes: u64,
    /// SHA-256, hex.
    pub sha256: String,
}

/// How the stored ELF compares with the captured internal flash.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ImageCheck {
    /// Every byte of every flash segment below the persistent region matches.
    Matches {
        /// Bytes compared.
        bytes: u64,
    },
    /// A byte differs.
    Differs {
        /// Bytes compared up to and including the first difference.
        bytes: u64,
        /// Address of the first differing byte.
        first_difference: u32,
    },
    /// No comparison was possible, and why.
    NotChecked(String),
}

/// What [`BuildFiles::store`] did, for the manifest.
#[derive(Debug, Clone)]
pub struct BuildRecord {
    /// [`BuildFiles::source_dir`].
    pub source_dir: PathBuf,
    /// The ELF, relative to the capture directory.
    pub elf: String,
    /// Files copied.
    pub files: Vec<StoredFile>,
    /// The region bounds found in the ELF (`__tag_code_limit__`,
    /// `__tag_config_start__`, `__tag_nand_map_start__`, `__flash0_end__`), in
    /// that order.
    pub symbols: Vec<(String, u32)>,
    /// The ELF against the captured internal flash.
    pub image_check: ImageCheck,
    /// Files that could not be copied or read, with why.
    pub errors: Vec<String>,
}

/// An absolute form of `p` for the manifest, or `p` itself.
fn absolute(p: &Path) -> PathBuf {
    std::fs::canonicalize(p).unwrap_or_else(|_| p.to_path_buf())
}

impl BuildFiles {
    /// Resolves `--with-build` and `--extra` into the files to store.
    ///
    /// `build` is a firmware build directory holding exactly one `*.elf`, or
    /// the ELF itself, whose directory is then searched for its companions.
    /// Reads only directory listings; nothing is copied.
    ///
    /// # Errors
    ///
    /// When `build` does not exist, a directory holds no ELF or more than
    /// one (the message lists them: name the one wanted), an extra file does
    /// not exist, or two files would be stored under the same name.
    pub fn resolve(build: &Path, extras: &[PathBuf]) -> Result<BuildFiles, String> {
        let (source_dir, elf) = if build.is_file() {
            let dir = build.parent().map_or_else(|| PathBuf::from("."), Path::to_path_buf);
            (dir, build.to_path_buf())
        } else if build.is_dir() {
            let mut elves: Vec<PathBuf> = std::fs::read_dir(build)
                .map_err(|e| format!("cannot list {}: {e}", build.display()))?
                .filter_map(|e| e.ok().map(|e| e.path()))
                .filter(|p| p.is_file() && p.extension().is_some_and(|x| x == "elf"))
                .collect();
            elves.sort();
            match elves.len() {
                1 => (build.to_path_buf(), elves.remove(0)),
                0 => return Err(format!("no .elf in {}", build.display())),
                _ => {
                    let names: Vec<String> = elves.iter().map(|p| p.display().to_string()).collect();
                    return Err(format!(
                        "{} holds several images; name one with --with-build: {}",
                        build.display(),
                        names.join(", ")
                    ));
                }
            }
        } else {
            return Err(format!("{} does not exist", build.display()));
        };

        let stem = elf.file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
        let mut files = vec![elf.clone()];
        for companion in [
            format!("{stem}.map"),
            format!("{stem}.list"),
            format!("{stem}-build-manifest.json"),
        ] {
            let p = source_dir.join(companion);
            if p.is_file() {
                files.push(p);
            }
        }
        for extra in extras {
            if !extra.is_file() {
                return Err(format!("{} does not exist", extra.display()));
            }
            files.push(extra.clone());
        }
        let mut names: Vec<_> = files.iter().filter_map(|p| p.file_name()).collect();
        names.sort();
        if let Some(w) = names.windows(2).find(|w| w[0] == w[1]) {
            return Err(format!(
                "two files named {} would be stored in {BUILD_SUBDIR}/",
                w[0].to_string_lossy()
            ));
        }
        Ok(BuildFiles { source_dir, elf, files })
    }

    /// Copies the files into `capture_dir/build/`, reads the ELF's region
    /// symbols, and compares the ELF with the captured internal flash.
    ///
    /// `flash` is the internal-flash image as captured and its base address,
    /// or `None` when it was not read. Only loadable segments that lie wholly
    /// inside it and below `__tag_code_limit__` are compared: the persistent
    /// region above that is the firmware's to rewrite, so a difference there
    /// says nothing about the image.
    ///
    /// Never fails: a file that cannot be copied or an ELF that cannot be
    /// read is recorded in [`BuildRecord::errors`], and the capture itself is
    /// unaffected.
    pub fn store(&self, capture_dir: &Path, flash: Option<(&[u8], u32)>) -> BuildRecord {
        let elf_name = |p: &Path| {
            format!("{BUILD_SUBDIR}/{}", p.file_name().unwrap_or_default().to_string_lossy())
        };
        let mut rec = BuildRecord {
            source_dir: absolute(&self.source_dir),
            elf: elf_name(&self.elf),
            files: Vec::new(),
            symbols: Vec::new(),
            image_check: ImageCheck::NotChecked(String::new()),
            errors: Vec::new(),
        };
        let out = capture_dir.join(BUILD_SUBDIR);
        if let Err(e) = std::fs::create_dir_all(&out) {
            rec.errors.push(format!("could not create {}: {e}", out.display()));
            rec.image_check = ImageCheck::NotChecked("nothing stored".into());
            return rec;
        }
        for src in &self.files {
            let data = match std::fs::read(src) {
                Ok(d) => d,
                Err(e) => {
                    rec.errors.push(format!("could not read {}: {e}", src.display()));
                    continue;
                }
            };
            let file = elf_name(src);
            if let Err(e) = std::fs::write(capture_dir.join(&file), &data) {
                rec.errors.push(format!("could not write {file}: {e}"));
                continue;
            }
            rec.files.push(StoredFile {
                file,
                source: absolute(src),
                bytes: data.len() as u64,
                sha256: sha256_hex(&data),
            });
        }

        rec.image_check = match TargetImage::load(&self.elf) {
            Err(e) => {
                rec.errors.push(e.clone());
                ImageCheck::NotChecked(format!("the ELF could not be read: {e}"))
            }
            Ok(image) => {
                rec.symbols = REGION_SYMBOLS
                    .iter()
                    .filter_map(|&n| image.symbol(n).map(|v| (n.to_string(), v)))
                    .collect();
                match flash {
                    None => ImageCheck::NotChecked("internal flash was not captured".into()),
                    Some((bytes, base)) => {
                        compare_image(&image, bytes, base, image.symbol("__tag_code_limit__"))
                    }
                }
            }
        };
        rec
    }
}

/// Compares the image's flash segments with captured flash `bytes` at
/// `base`, up to `code_limit` when known.
fn compare_image(image: &TargetImage, bytes: &[u8], base: u32, code_limit: Option<u32>) -> ImageCheck {
    let end = u64::from(base) + bytes.len() as u64;
    let limit = code_limit.map_or(end, |l| u64::from(l).min(end));
    let mut compared = 0u64;
    for seg in image.segments() {
        let start = u64::from(seg.addr);
        let stop = start + seg.data.len() as u64;
        if seg.data.is_empty() || start < u64::from(base) || stop > limit {
            continue;
        }
        let off = (start - u64::from(base)) as usize;
        let captured = &bytes[off..off + seg.data.len()];
        if let Some(i) = seg.data.iter().zip(captured).position(|(a, b)| a != b) {
            return ImageCheck::Differs {
                bytes: compared + i as u64 + 1,
                first_difference: seg.addr + i as u32,
            };
        }
        compared += seg.data.len() as u64;
    }
    if compared == 0 {
        ImageCheck::NotChecked("no segment of the image lies in the captured code region".into())
    } else {
        ImageCheck::Matches { bytes: compared }
    }
}

impl BuildRecord {
    /// The manifest's `"build"` member, as JSON text; `quote` escapes a
    /// string as the rest of the manifest does.
    pub(crate) fn to_json(&self, quote: &dyn Fn(&str) -> String) -> String {
        let mut j = String::from("{");
        let _ = write!(j, "\"source_dir\": {}, ", quote(&self.source_dir.display().to_string()));
        let _ = write!(j, "\"elf\": {}, \"files\": [", quote(&self.elf));
        let files: Vec<String> = self
            .files
            .iter()
            .map(|f| {
                format!(
                    "{{\"file\": {}, \"source\": {}, \"bytes\": {}, \"sha256\": {}}}",
                    quote(&f.file),
                    quote(&f.source.display().to_string()),
                    f.bytes,
                    quote(&f.sha256)
                )
            })
            .collect();
        j += &files.join(", ");
        j += "], \"symbols\": {";
        let syms: Vec<String> = self
            .symbols
            .iter()
            .map(|(n, v)| format!("{}: {}", quote(n), quote(&format!("0x{v:08X}"))))
            .collect();
        j += &syms.join(", ");
        j += "}, \"image_check\": ";
        j += &match &self.image_check {
            ImageCheck::Matches { bytes } => {
                format!("{{\"result\": \"matches\", \"bytes_compared\": {bytes}}}")
            }
            ImageCheck::Differs { bytes, first_difference } => format!(
                "{{\"result\": \"differs\", \"bytes_compared\": {bytes}, \"first_difference\": {}}}",
                quote(&format!("0x{first_difference:08X}"))
            ),
            ImageCheck::NotChecked(why) => {
                format!("{{\"result\": \"not_checked\", \"note\": {}}}", quote(why))
            }
        };
        let errors: Vec<String> = self.errors.iter().map(|e| quote(e)).collect();
        let _ = write!(j, ", \"errors\": [{}]}}", errors.join(", "));
        j
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A segment wholly below the code limit is compared byte for byte.
    #[test]
    fn compare_matches_and_differs() {
        let image = TargetImage::from_segments_for_test(vec![(0x0800_0000, vec![1, 2, 3, 4])]);
        let flash = [1u8, 2, 3, 4, 0xFF, 0xFF];
        assert_eq!(compare_image(&image, &flash, 0x0800_0000, None), ImageCheck::Matches { bytes: 4 });
        let flash = [1u8, 2, 9, 4, 0xFF, 0xFF];
        assert_eq!(
            compare_image(&image, &flash, 0x0800_0000, None),
            ImageCheck::Differs { bytes: 3, first_difference: 0x0800_0002 }
        );
    }

    /// Segments in the persistent region, or outside the captured flash,
    /// are not compared.
    #[test]
    fn compare_skips_persistent_and_outside() {
        let image = TargetImage::from_segments_for_test(vec![
            (0x0800_0000, vec![1, 2]),
            (0x0800_0004, vec![7, 7]),
            (0x2000_0000, vec![5]),
        ]);
        let flash = [1u8, 2, 0, 0, 0, 0];
        assert_eq!(
            compare_image(&image, &flash, 0x0800_0000, Some(0x0800_0004)),
            ImageCheck::Matches { bytes: 2 }
        );
        assert!(matches!(
            compare_image(&image, &flash, 0x0800_0000, Some(0x0800_0000)),
            ImageCheck::NotChecked(_)
        ));
    }
}
