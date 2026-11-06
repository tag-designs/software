//! External-flash capture through a loader's `Serve()`.
//!
//! Port of `tagcore/recovery/externalcapture.cc`; the files and their
//! contents are the same, so a Rust capture reads exactly as a C++ one.
//!
//! - A linear part (NOR) is read whole into `<prefix>flash.bin`.
//! - A paged part (SPI NAND) is read page by page: page 0 of each block raw;
//!   a blank block (every byte `FFh`) is skipped unless `full`; every page of
//!   any other block is read raw into `<prefix>raw.bin` and through ECC into
//!   `<prefix>ecc.bin`, with one line per page in `<prefix>pages.csv`.
//!
//! Every file written is hashed, so a manifest can record it. The loader
//! overwrites the start of SRAM1: capture SRAM first.

use std::fs::File;
use std::io::Write as _;
use std::path::Path;
use std::time::Instant;

use sha2::{Digest, Sha256};

use super::image::TargetImage;
use super::session::Session;
use super::xflash::ExternalFlash;

/// How much of the part to read, and where to report progress.
#[derive(Default)]
pub struct ExternalCaptureOptions<'a> {
    /// File-name prefix, e.g. `"external_"`; may be empty.
    pub prefix: String,
    /// Paged parts: read every block, blank or not.
    pub full: bool,
    /// Paged parts: first block to scan.
    pub first_block: u32,
    /// Paged parts: blocks to scan; 0 means to the end.
    pub block_count: u32,
    /// Called with one line per progress update.
    pub progress: Option<&'a mut dyn FnMut(&str)>,
}

/// One file written.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ExternalCaptureFile {
    /// Role: `"external_flash"`, `"raw"`, `"ecc"` or `"pages"`.
    pub name: String,
    /// File name within the directory.
    pub file: String,
    /// Bytes.
    pub size: u64,
    /// SHA-256 of the file as written, lower-case hex.
    pub sha256: String,
}

/// What the capture read and found.
#[derive(Debug, Clone, Default)]
pub struct ExternalCaptureResult {
    /// Everything requested was read.
    pub ok: bool,
    /// Why not.
    pub error: String,
    /// Loader image used.
    pub loader_path: String,
    /// SHA-256 of the loader image file.
    pub loader_sha256: String,
    /// `Serve()` protocol version.
    pub version: u32,
    /// JEDEC ID as read, manufacturer in bits 23:16.
    pub jedec: u32,
    /// Status register as found (NOR: SR1; NAND: C0h).
    pub sr1: u32,
    /// Paged parts: `A0 | B0 << 8 | C0 << 16 | F0 << 24` as found.
    pub found: u32,
    /// The part was read page by page.
    pub paged: bool,
    /// Data bytes in the part.
    pub size: u32,
    /// Paged: bytes per page including spare.
    pub page_bytes: u32,
    /// Paged: pages per erase block.
    pub pages_per_block: u32,
    /// Paged: blocks whose page 0 was read.
    pub blocks_scanned: u32,
    /// Paged: blocks read whole.
    pub blocks_read: u32,
    /// Paged: blocks skipped as blank.
    pub blocks_blank: u32,
    /// Paged: read blocks whose page 0 spare byte 0 is not `FFh`.
    pub blocks_marked: u32,
    /// Paged: pages whose ECC verdict was uncorrectable.
    pub pages_uncorrectable: u32,
    /// Wall time in seconds.
    pub seconds: f64,
    /// Files written.
    pub files: Vec<ExternalCaptureFile>,
}

/// Lower-case hex of a digest.
pub(crate) fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

/// SHA-256 of `data`, lower-case hex.
pub(crate) fn sha256_hex(data: &[u8]) -> String {
    hex(&Sha256::digest(data))
}

/// A file being written and hashed as it goes.
struct HashedFile {
    /// The open file.
    f: File,
    /// File name within the directory.
    file: String,
    /// The running hash.
    sha: Sha256,
    /// Bytes written.
    size: u64,
}

impl HashedFile {
    /// Creates `dir/file`.
    fn create(dir: &Path, file: String) -> Result<HashedFile, String> {
        let f = File::create(dir.join(&file))
            .map_err(|e| format!("cannot write to {}: {e}", dir.display()))?;
        Ok(HashedFile { f, file, sha: Sha256::new(), size: 0 })
    }

    /// Appends `data`.
    fn write(&mut self, data: &[u8]) -> Result<(), String> {
        self.sha.update(data);
        self.size += data.len() as u64;
        self.f.write_all(data).map_err(|e| format!("write to {} failed: {e}", self.file))
    }

    /// Closes the file and describes it under the role `name`.
    fn finish(self, name: &str) -> ExternalCaptureFile {
        ExternalCaptureFile {
            name: name.into(),
            file: self.file,
            size: self.size,
            sha256: hex(&self.sha.finalize()),
        }
    }
}

/// Sends a progress line, if anyone is listening.
fn progress(o: &mut ExternalCaptureOptions, line: &str) {
    if let Some(p) = o.progress.as_deref_mut() {
        p(line);
    }
}

/// Reads the external flash through `loader` into `dir`, an existing
/// directory.
///
/// The session must be open with the core halted at the reset vector. The
/// result is returned even on failure, with `ok` false and `error` set, so
/// a manifest can record how far it got.
pub fn capture_external_flash(
    s: &mut Session,
    loader: &TargetImage,
    dir: &Path,
    o: &mut ExternalCaptureOptions,
) -> ExternalCaptureResult {
    let mut r = ExternalCaptureResult {
        loader_path: loader.path().display().to_string(),
        loader_sha256: std::fs::read(loader.path())
            .map(|b| sha256_hex(&b))
            .unwrap_or_else(|_| sha256_hex(&[])),
        ..Default::default()
    };
    let t0 = Instant::now();

    let mut xf = match ExternalFlash::open(s, loader) {
        Ok(xf) => xf,
        Err(e) => {
            r.error = format!("loader failed: {e}");
            return r;
        }
    };
    r.version = xf.version();
    r.jedec = xf.jedec();
    r.sr1 = xf.sr1();
    r.found = xf.found();
    r.size = xf.size();

    let read = if xf.page_bytes() != 0 {
        capture_paged(s, &mut xf, dir, o, &mut r)
    } else {
        capture_linear(s, &mut xf, dir, o, &mut r)
    };
    let closed = xf.close(s);
    r.seconds = t0.elapsed().as_secs_f64();
    match (read, closed) {
        (Err(e), _) => r.error = e,
        (Ok(()), Err(e)) => {
            r.error = format!("Serve() did not exit cleanly (configuration not restored?): {e}")
        }
        (Ok(()), Ok(())) => r.ok = true,
    }
    r
}

/// Reads a linear part whole, 64 KB at a time.
fn capture_linear(
    s: &mut Session,
    xf: &mut ExternalFlash,
    dir: &Path,
    o: &mut ExternalCaptureOptions,
    r: &mut ExternalCaptureResult,
) -> Result<(), String> {
    const CHUNK: u32 = 64 * 1024;
    let mut out = HashedFile::create(dir, format!("{}flash.bin", o.prefix))?;
    let mut buf = vec![0u8; CHUNK as usize];
    let mut next_report = 0;
    let mut done = 0u32;
    while done < r.size {
        let n = CHUNK.min(r.size - done) as usize;
        xf.read(s, done, &mut buf[..n], None)
            .map_err(|e| format!("read at offset {done}: {e}"))?;
        out.write(&buf[..n])
            .map_err(|_| format!("write to {} failed", dir.display()))?;
        done += n as u32;
        if done >= next_report || done == r.size {
            progress(o, &format!("{done} / {} bytes", r.size));
            next_report = done + 512 * 1024;
        }
    }
    r.files.push(out.finish("external_flash"));
    Ok(())
}

/// Reads a paged part block by block; see the module description.
fn capture_paged(
    s: &mut Session,
    xf: &mut ExternalFlash,
    dir: &Path,
    o: &mut ExternalCaptureOptions,
    r: &mut ExternalCaptureResult,
) -> Result<(), String> {
    const VERDICT: [&str; 4] = ["ok", "corrected", "uncorrectable", "corrected8"];
    r.paged = true;
    r.page_bytes = xf.page_bytes();
    // The data area: the largest power of 2 in a page.
    let mut data_bytes = 1u32;
    while data_bytes * 2 <= r.page_bytes {
        data_bytes *= 2;
    }
    r.pages_per_block = xf.sector_size() / data_bytes;
    let blocks = xf.size() / xf.sector_size();
    let mut count = o.block_count;
    if count == 0 || o.first_block + count > blocks {
        count = blocks.saturating_sub(o.first_block);
    }

    let mut raw = HashedFile::create(dir, format!("{}raw.bin", o.prefix))?;
    let mut ecc = HashedFile::create(dir, format!("{}ecc.bin", o.prefix))?;
    let mut csv = HashedFile::create(dir, format!("{}pages.csv", o.prefix))?;
    csv.write(b"page,block,raw_c0,ecc_c0,ecc_f0,ecc_verdict\n")?;

    let mut page = vec![0u8; r.page_bytes as usize];
    let mut page_ecc = vec![0u8; r.page_bytes as usize];
    for b in o.first_block..o.first_block + count {
        let p0 = b * r.pages_per_block;
        let (st, _) = xf
            .read_page(s, p0, true, &mut page)
            .map_err(|e| format!("page {p0} (raw): {e}"))?;
        r.blocks_scanned += 1;
        let blank = page.iter().all(|&v| v == 0xFF);
        if blank && !o.full {
            r.blocks_blank += 1;
        } else {
            if page[data_bytes as usize] != 0xFF {
                // Spare byte 0 of page 0: the factory bad-block mark.
                r.blocks_marked += 1;
            }
            r.blocks_read += 1;
            for i in 0..r.pages_per_block {
                let pg = p0 + i;
                let mut raw_st = st;
                if i > 0 {
                    raw_st = xf
                        .read_page(s, pg, true, &mut page)
                        .map_err(|e| format!("page {pg} (raw): {e}"))?
                        .0;
                }
                let (ecc_st, ecc_st2) = xf
                    .read_page(s, pg, false, &mut page_ecc)
                    .map_err(|e| format!("page {pg} (ECC): {e}"))?;
                let verdict = ((ecc_st >> 4) & 3) as usize;
                if verdict == 2 {
                    r.pages_uncorrectable += 1;
                }
                raw.write(&page)?;
                ecc.write(&page_ecc)?;
                csv.write(
                    format!(
                        "{pg},{b},0x{raw_st:02X},0x{ecc_st:02X},0x{ecc_st2:02X},{}\n",
                        VERDICT[verdict]
                    )
                    .as_bytes(),
                )?;
            }
        }
        if (b - o.first_block) % 64 == 63 || b + 1 == o.first_block + count {
            progress(
                o,
                &format!(
                    "block {}/{count}  read {}  blank {}",
                    b + 1 - o.first_block,
                    r.blocks_read,
                    r.blocks_blank
                ),
            );
        }
    }
    r.files.push(raw.finish("raw"));
    r.files.push(ecc.finish("ecc"));
    r.files.push(csv.finish("pages"));
    Ok(())
}
