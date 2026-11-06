//! A tag's external flash, read (and in rescue, erased and programmed)
//! through a loader's `Serve()` entry point.
//!
//! Port of `tagcore/recovery/externalflash.cc`. [`ExternalFlash::open`]
//! downloads the loader into SRAM1 and starts `Serve(buffer, size)`, which
//! initialises the part once and then runs commands from the
//! `loaderService` block (`include/loader_service.h`). The host submits a
//! command by writing its fields and incrementing `seq`, and waits for
//! `ack` over SWD while the core runs. [`ExternalFlash::close`] sends EXIT
//! and waits for `Serve()` to return into the trap.
//!
//! Compared with the per-call ST convention ([`SramCall::call`] on `Init`
//! and `Read`), this initialises once, costs no register setup per command,
//! and reports the part's JEDEC ID, its status register as found, and where
//! a command failed.

use std::time::{Duration, Instant};

use super::image::TargetImage;
use super::session::Session;
use super::sramcall::SramCall;

/// The layout of `include/loader_service.h`, as byte offsets into
/// `LoaderServiceBlock`. Little-endian 32-bit words, no padding; checked
/// against the header by this module's tests.
pub mod layout {
    /// `'LSRV'`: written to `magic` once the loader is ready for commands.
    pub const MAGIC_VALUE: u32 = 0x5652_534C;
    /// The layout version this host speaks; it also accepts 1.
    pub const VERSION: u32 = 2;
    /// Offset of `magic`.
    pub const MAGIC: u32 = 0;
    /// Offset of `version`.
    pub const VERSION_FIELD: u32 = 4;
    /// Offset of `seq`: the host increments it to submit a command.
    pub const SEQ: u32 = 8;
    /// Offset of `ack`: the loader copies `seq` when the command completes.
    pub const ACK: u32 = 12;
    /// Offset of `cmd`.
    pub const CMD: u32 = 16;
    /// Offset of `offset` (a flash byte offset).
    pub const OFFSET: u32 = 20;
    /// Offset of `length`.
    pub const LENGTH: u32 = 24;
    /// Offset of `status`, an `int32_t`.
    pub const STATUS: u32 = 28;
    /// Offset of `detail[0]`.
    pub const DETAIL: u32 = 32;
    /// Words in `detail[]` (version 2; version 1 had 8).
    pub const DETAIL_WORDS: u32 = 12;
    /// Offset of `progress`.
    pub const PROGRESS: u32 = DETAIL + 4 * DETAIL_WORDS;
    /// `sizeof(LoaderServiceBlock)`.
    pub const SIZE: u32 = PROGRESS + 4;

    /// `LOADER_CMD_PROBE`: re-read identity into `detail[]`.
    pub const CMD_PROBE: u32 = 1;
    /// `LOADER_CMD_READ`: read `length` bytes at `offset` into the buffer.
    pub const CMD_READ: u32 = 2;
    /// `LOADER_CMD_ERASE_SECTOR`: erase the sector holding `offset`.
    pub const CMD_ERASE_SECTOR: u32 = 3;
    /// `LOADER_CMD_PROGRAM`: program `length` buffer bytes at `offset`.
    pub const CMD_PROGRAM: u32 = 4;
    /// `LOADER_CMD_EXIT`: return from `Serve()` into the host's trap.
    pub const CMD_EXIT: u32 = 5;
    /// `LOADER_CMD_READ_PAGE`: read page `offset`, data and spare.
    pub const CMD_READ_PAGE: u32 = 6;

    /// `LOADER_PAGE_ECC`: a page read through the part's on-die ECC.
    pub const PAGE_ECC: u32 = 0;
    /// `LOADER_PAGE_RAW`: a page read with ECC off.
    pub const PAGE_RAW: u32 = 1;

    /// `detail[]` index of the JEDEC ID, manufacturer in bits 23:16.
    pub const DETAIL_JEDEC: u32 = 0;
    /// `detail[]` index of status register 1 as found.
    pub const DETAIL_SR1: u32 = 1;
    /// `detail[]` index of the part size in bytes.
    pub const DETAIL_SIZE: u32 = 2;
    /// `detail[]` index of the erase sector size in bytes.
    pub const DETAIL_SECTOR: u32 = 3;
    /// `detail[]` index of the writable flag.
    pub const DETAIL_WRITABLE: u32 = 4;
    /// `detail[]` index of a failed command's flash offset.
    pub const DETAIL_FAIL_OFFSET: u32 = 5;
    /// `detail[]` index of the status after the last READ_PAGE (NAND C0h).
    pub const DETAIL_PAGE_STATUS: u32 = 6;
    /// `detail[]` index of the second status after it (NAND F0h).
    pub const DETAIL_PAGE_STATUS2: u32 = 7;
    /// `detail[]` index of the bytes per page including spare.
    pub const DETAIL_PAGE_BYTES: u32 = 8;
    /// `detail[]` index of the part's configuration as found.
    pub const DETAIL_FOUND: u32 = 9;
}

use layout as l;

/// Longest wait for `Serve()` to initialise: clock, board, SPI, flash wake.
const READY_TIMEOUT: Duration = Duration::from_millis(2000);
/// Per-command budget for a read.
const READ_TIMEOUT: Duration = Duration::from_millis(5000);
/// Per-command budget for an erase, bounded by the part driver's own.
const ERASE_TIMEOUT: Duration = Duration::from_millis(10000);
/// Per-command budget for a program.
const PROGRAM_TIMEOUT: Duration = Duration::from_millis(10000);

/// The text for a `LoaderServiceStatus` value.
fn status_name(status: i32) -> &'static str {
    match status {
        0 => "ok",
        -1 => "unknown command",
        -2 => "out of range",
        -3 => "SPI timeout or no response",
        -4 => "read-only loader",
        -5 => "did not read back",
        -6 => "loader initialisation failed",
        _ => "unknown status",
    }
}

/// A running `Serve()` session on an open [`Session`].
///
/// Every call takes the session, which must stay open throughout. Not
/// thread-safe. If the value is dropped without [`close`](Self::close), the
/// core is left running `Serve()` until the session closes, which halts and
/// resets it.
#[derive(Debug)]
pub struct ExternalFlash {
    /// The download and call frame.
    call: SramCall,
    /// True from a successful open until close or a command timeout.
    open: bool,
    /// Address of the `loaderService` block.
    block: u32,
    /// The last sequence number submitted.
    seq: u32,
    /// JEDEC ID read at open.
    jedec: u32,
    /// Status register 1 as found at open.
    sr1: u32,
    /// Part size in bytes.
    size: u32,
    /// Erase sector size in bytes.
    sector: u32,
    /// Bytes per page including spare, or 0.
    page_bytes: u32,
    /// The part's configuration as found.
    found: u32,
    /// Service protocol version.
    version: u32,
    /// The image can erase and program.
    writable: bool,
}

impl ExternalFlash {
    /// Downloads `loader`, starts `Serve()` and waits until it is ready.
    ///
    /// The service block is zeroed before the start: it is in `.bss`, which
    /// is not downloaded, and SRAM survives reset, so a magic left by an
    /// earlier session would read as ready before `Serve()` cleared it.
    ///
    /// # Errors
    ///
    /// The loader has no `loaderService` block (an older loader: use the ST
    /// entry points); the download or start failed; `Serve()` did not become
    /// ready within 2 s; the block could not be read; its version is neither
    /// 1 nor 2; or the loader reported an initialisation failure. After a
    /// failure once `Serve()` has started, the core is halted.
    pub fn open(s: &mut Session, loader: &TargetImage) -> Result<ExternalFlash, String> {
        let block = loader.symbol("loaderService").ok_or_else(|| {
            format!(
                "{} has no Serve() service block (an older loader: use the ST entry points)",
                loader.path().display()
            )
        })?;
        let mut call = SramCall::download(s, loader)?;
        let zero = vec![0u8; align4(l::SIZE) as usize];
        if !s.write(block, &zero) {
            return Err("could not clear the service block".into());
        }
        let (buf, size) = (call.buffer_address(), call.buffer_size());
        call.start(s, "Serve", &[buf, size])?;

        let deadline = Instant::now() + READY_TIMEOUT;
        while s.read_word(block + l::MAGIC) != Some(l::MAGIC_VALUE) {
            if Instant::now() >= deadline {
                s.halt();
                return Err("Serve() did not become ready".into());
            }
            std::thread::sleep(Duration::from_millis(1));
        }

        let (Some(version), Some(status), Some(seq)) = (
            s.read_word(block + l::VERSION_FIELD),
            s.read_word(block + l::STATUS),
            s.read_word(block + l::SEQ),
        ) else {
            s.halt();
            return Err("could not read the service block".into());
        };
        let mut d = [0u32; l::DETAIL_WORDS as usize];
        for (i, w) in d.iter_mut().enumerate() {
            *w = s.read_word(block + l::DETAIL + 4 * i as u32).unwrap_or(0);
        }
        let mut xf = ExternalFlash {
            call,
            open: false,
            block,
            seq,
            jedec: d[l::DETAIL_JEDEC as usize],
            sr1: d[l::DETAIL_SR1 as usize],
            size: d[l::DETAIL_SIZE as usize],
            sector: d[l::DETAIL_SECTOR as usize],
            writable: d[l::DETAIL_WRITABLE as usize] != 0,
            version,
            // Version 1 had an 8-word detail[]: the paged fields are not there.
            page_bytes: if version >= 2 { d[l::DETAIL_PAGE_BYTES as usize] } else { 0 },
            found: if version >= 2 { d[l::DETAIL_FOUND as usize] } else { 0 },
        };

        if version != 1 && version != l::VERSION {
            s.halt();
            return Err(format!(
                "service block version {version}, expected 1 or {}",
                l::VERSION
            ));
        }
        if status as i32 != 0 {
            let _ = xf.call.wait_return(s, Duration::from_millis(100));
            return Err(format!(
                "{} (JEDEC 0x{:06X})",
                status_name(status as i32),
                xf.jedec
            ));
        }
        xf.open = true;
        Ok(xf)
    }

    /// JEDEC ID read at open, manufacturer in bits 23:16.
    pub fn jedec(&self) -> u32 {
        self.jedec
    }

    /// Status register 1 as found at open, before any command.
    pub fn sr1(&self) -> u32 {
        self.sr1
    }

    /// Part size in bytes.
    pub fn size(&self) -> u32 {
        self.size
    }

    /// Erase sector size in bytes.
    pub fn sector_size(&self) -> u32 {
        self.sector
    }

    /// True when the loader image can erase and program.
    pub fn writable(&self) -> bool {
        self.writable
    }

    /// Bytes per page including spare, or 0 for a part without pages.
    pub fn page_bytes(&self) -> u32 {
        self.page_bytes
    }

    /// The part's configuration as found (for NAND:
    /// `A0 | B0 << 8 | C0 << 16 | F0 << 24`).
    pub fn found(&self) -> u32 {
        self.found
    }

    /// Service protocol version the loader reported.
    pub fn version(&self) -> u32 {
        self.version
    }

    /// Submits one command and waits up to `timeout` for its ack.
    ///
    /// The fields are written first and `seq` last, because writing `seq`
    /// submits the command. A timeout halts the core and ends the session.
    fn command(
        &mut self,
        s: &mut Session,
        cmd: u32,
        offset: u32,
        length: u32,
        timeout: Duration,
    ) -> Result<(), String> {
        if !self.open {
            return Err("not open".into());
        }
        self.seq = self.seq.wrapping_add(1);
        if !s.write_word(self.block + l::CMD, cmd)
            || !s.write_word(self.block + l::OFFSET, offset)
            || !s.write_word(self.block + l::LENGTH, length)
            || !s.write_word(self.block + l::SEQ, self.seq)
        {
            return Err("could not submit the command".into());
        }
        let deadline = Instant::now() + timeout;
        while s.read_word(self.block + l::ACK) != Some(self.seq) {
            if Instant::now() >= deadline {
                s.halt();
                self.open = false;
                return Err(format!(
                    "command {cmd} not acknowledged within {} ms; core halted",
                    timeout.as_millis()
                ));
            }
        }
        let status = s
            .read_word(self.block + l::STATUS)
            .ok_or("could not read the command status")? as i32;
        if status != 0 {
            let at = s
                .read_word(self.block + l::DETAIL + 4 * l::DETAIL_FAIL_OFFSET)
                .unwrap_or(0);
            return Err(format!(
                "{} at offset {}",
                status_name(status),
                if at != 0 { at } else { offset }
            ));
        }
        Ok(())
    }

    /// Reads `out.len()` bytes at flash offset `offset`, one buffer at a
    /// time, calling `progress(done, total)` after each.
    ///
    /// # Errors
    ///
    /// A failed command (see the loader's status) or SWD read of the buffer.
    pub fn read(
        &mut self,
        s: &mut Session,
        offset: u32,
        out: &mut [u8],
        mut progress: Option<&mut dyn FnMut(u32, u32)>,
    ) -> Result<(), String> {
        let (buf, max) = (self.call.buffer_address(), self.call.buffer_size() as usize);
        let total = out.len() as u32;
        let mut done = 0usize;
        for part in out.chunks_mut(max) {
            let n = part.len();
            self.command(s, l::CMD_READ, offset + done as u32, n as u32, READ_TIMEOUT)?;
            let mut tmp = vec![0u8; align4(n as u32) as usize];
            if !s.read(buf, &mut tmp, 8192, None) {
                return Err("SWD read of the buffer failed".into());
            }
            part.copy_from_slice(&tmp[..n]);
            done += n;
            if let Some(p) = progress.as_deref_mut() {
                p(done as u32, total);
            }
        }
        Ok(())
    }

    /// Reads one whole page, data and spare, from a paged part.
    ///
    /// `raw` turns the part's on-die ECC off (the bytes as stored). Returns
    /// [`page_bytes`](Self::page_bytes) bytes and the part's two status
    /// registers after the read (NAND: C0h, whose bits 5:4 are the ECC
    /// verdict, and F0h).
    ///
    /// # Errors
    ///
    /// A part without pages or a version-1 loader; a failed command; or an
    /// SWD read that failed.
    pub fn read_page(
        &mut self,
        s: &mut Session,
        page: u32,
        raw: bool,
        out: &mut [u8],
    ) -> Result<(u8, u8), String> {
        if self.page_bytes == 0 || self.version < 2 {
            return Err("the loader does not read whole pages".into());
        }
        let mode = if raw { l::PAGE_RAW } else { l::PAGE_ECC };
        self.command(s, l::CMD_READ_PAGE, page, mode, READ_TIMEOUT)?;
        let (Some(st), Some(st2)) = (
            s.read_word(self.block + l::DETAIL + 4 * l::DETAIL_PAGE_STATUS),
            s.read_word(self.block + l::DETAIL + 4 * l::DETAIL_PAGE_STATUS2),
        ) else {
            return Err("could not read the page status".into());
        };
        let n = self.page_bytes as usize;
        let mut tmp = vec![0u8; align4(self.page_bytes) as usize];
        if !s.read(self.call.buffer_address(), &mut tmp, 8192, None) {
            return Err("SWD read of the buffer failed".into());
        }
        out[..n].copy_from_slice(&tmp[..n]);
        Ok((st as u8, st2 as u8))
    }

    /// Erases the sector holding `offset`; the loader proves it blank.
    ///
    /// # Errors
    ///
    /// A read-only loader, a range error, or a failed erase.
    pub fn erase_sector(&mut self, s: &mut Session, offset: u32) -> Result<(), String> {
        self.command(s, l::CMD_ERASE_SECTOR, offset, 0, ERASE_TIMEOUT)
    }

    /// Programs `data` at `offset`, one buffer at a time, each verified by
    /// read-back in the loader. A partial last word is padded with `0xFF`.
    ///
    /// # Errors
    ///
    /// A read-only loader, a range error, a failed program, or an SWD write
    /// of the buffer that failed.
    pub fn program(&mut self, s: &mut Session, offset: u32, data: &[u8]) -> Result<(), String> {
        let (buf, max) = (self.call.buffer_address(), self.call.buffer_size() as usize);
        let mut done = 0usize;
        for part in data.chunks(max) {
            let mut tmp = vec![0xFFu8; align4(part.len() as u32) as usize];
            tmp[..part.len()].copy_from_slice(part);
            if !s.write(buf, &tmp) {
                return Err("SWD write of the buffer failed".into());
            }
            self.command(s, l::CMD_PROGRAM, offset + done as u32, part.len() as u32, PROGRAM_TIMEOUT)?;
            done += part.len();
        }
        Ok(())
    }

    /// Asks `Serve()` to return, and waits up to a second for it.
    ///
    /// Succeeds at once if the session already ended (closed, or a command
    /// timed out).
    ///
    /// # Errors
    ///
    /// EXIT could not be sent (the core is halted), `Serve()` did not
    /// return into the trap, or it returned other than 1.
    pub fn close(&mut self, s: &mut Session) -> Result<(), String> {
        if !self.open {
            return Ok(());
        }
        self.open = false;
        self.seq = self.seq.wrapping_add(1);
        if !s.write_word(self.block + l::CMD, l::CMD_EXIT) || !s.write_word(self.block + l::SEQ, self.seq) {
            s.halt();
            return Err("could not send EXIT".into());
        }
        match self.call.wait_return(s, Duration::from_millis(1000))? {
            1 => Ok(()),
            r0 => Err(format!("Serve() returned {r0}")),
        }
    }
}

/// Rounds a byte count up to whole words.
fn align4(n: u32) -> u32 {
    (n + 3) & !3
}

/// Checks [`layout`] against `include/loader_service.h`, the header the
/// loaders are built from.
#[cfg(test)]
mod tests {
    use super::layout as l;

    /// The header's text.
    fn header() -> String {
        let path = concat!(env!("CARGO_MANIFEST_DIR"), "/../../../include/loader_service.h");
        std::fs::read_to_string(path).expect("include/loader_service.h")
    }

    /// The value of `#define NAME value` (a `U` suffix allowed), or of an
    /// enumerator `NAME = value,`.
    fn value(h: &str, name: &str) -> i64 {
        for line in h.lines() {
            let t = line.trim();
            let rest = t
                .strip_prefix("#define ")
                .and_then(|r| r.strip_prefix(name))
                .filter(|r| r.starts_with(' '))
                .or_else(|| t.strip_prefix(name).and_then(|r| r.trim_start().strip_prefix('=')));
            if let Some(rest) = rest {
                let v = rest.trim().trim_end_matches(',').split_whitespace().next().unwrap();
                let v = v.trim_end_matches(',').trim_end_matches('U');
                return if let Some(hex) = v.strip_prefix("0x") {
                    i64::from_str_radix(hex, 16).unwrap()
                } else {
                    v.parse().unwrap()
                };
            }
        }
        panic!("{name} not in loader_service.h");
    }

    /// Field offsets, computed from the struct's members in order: every
    /// member is a 32-bit word or an array of them.
    #[test]
    fn struct_layout_matches_header() {
        let h = header();
        let body = h.split("typedef struct {").nth(1).unwrap().split("} LoaderServiceBlock;").next().unwrap();
        let mut offsets = Vec::new();
        let mut off = 0u32;
        for line in body.lines() {
            let decl = line.split("///").next().unwrap().trim();
            let Some(decl) = decl.strip_suffix(';') else { continue };
            let name = decl.split_whitespace().last().unwrap();
            let (name, words) = match name.split_once('[') {
                Some((n, count)) => (n, count.trim_end_matches(']').parse::<u32>().unwrap()),
                None => (name, 1),
            };
            offsets.push((name.to_string(), off));
            off += 4 * words;
        }
        let at = |n: &str| offsets.iter().find(|(m, _)| m == n).unwrap().1;
        assert_eq!(at("magic"), l::MAGIC);
        assert_eq!(at("version"), l::VERSION_FIELD);
        assert_eq!(at("seq"), l::SEQ);
        assert_eq!(at("ack"), l::ACK);
        assert_eq!(at("cmd"), l::CMD);
        assert_eq!(at("offset"), l::OFFSET);
        assert_eq!(at("length"), l::LENGTH);
        assert_eq!(at("status"), l::STATUS);
        assert_eq!(at("detail"), l::DETAIL);
        assert_eq!(at("progress"), l::PROGRESS);
        assert_eq!(off, l::SIZE);
    }

    /// Every constant the host uses has the header's value.
    #[test]
    fn constants_match_header() {
        let h = header();
        let checks: [(&str, i64); 23] = [
            ("LOADER_SERVICE_MAGIC", l::MAGIC_VALUE.into()),
            ("LOADER_SERVICE_VERSION", l::VERSION.into()),
            ("LOADER_CMD_PROBE", l::CMD_PROBE.into()),
            ("LOADER_CMD_READ", l::CMD_READ.into()),
            ("LOADER_CMD_ERASE_SECTOR", l::CMD_ERASE_SECTOR.into()),
            ("LOADER_CMD_PROGRAM", l::CMD_PROGRAM.into()),
            ("LOADER_CMD_EXIT", l::CMD_EXIT.into()),
            ("LOADER_CMD_READ_PAGE", l::CMD_READ_PAGE.into()),
            ("LOADER_PAGE_ECC", l::PAGE_ECC.into()),
            ("LOADER_PAGE_RAW", l::PAGE_RAW.into()),
            ("LOADER_DETAIL_JEDEC", l::DETAIL_JEDEC.into()),
            ("LOADER_DETAIL_SR1", l::DETAIL_SR1.into()),
            ("LOADER_DETAIL_SIZE", l::DETAIL_SIZE.into()),
            ("LOADER_DETAIL_SECTOR", l::DETAIL_SECTOR.into()),
            ("LOADER_DETAIL_WRITABLE", l::DETAIL_WRITABLE.into()),
            ("LOADER_DETAIL_FAIL_OFFSET", l::DETAIL_FAIL_OFFSET.into()),
            ("LOADER_DETAIL_PAGE_STATUS", l::DETAIL_PAGE_STATUS.into()),
            ("LOADER_DETAIL_PAGE_STATUS2", l::DETAIL_PAGE_STATUS2.into()),
            ("LOADER_DETAIL_PAGE_BYTES", l::DETAIL_PAGE_BYTES.into()),
            ("LOADER_DETAIL_FOUND", l::DETAIL_FOUND.into()),
            ("LOADER_STATUS_OK", 0),
            ("LOADER_STATUS_RANGE", -2),
            ("LOADER_STATUS_INIT", -6),
        ];
        for (name, want) in checks {
            assert_eq!(value(&h, name), want, "{name}");
        }
    }
}
