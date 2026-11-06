//! Capture a tag's registers, internal flash, SRAM and external flash over
//! SWD without booting it.
//!
//! Ported from the deleted C++ `tagcore/recovery/statecapture.cc`. The first thing done to a
//! returned tag. Reads, in order of how much they disturb the tag:
//!
//! 1. registers: `FLASH_OPTR`, the flash ECC registers (before the flash is
//!    read, since reading can overwrite them), `RCC_CSR` reset flags,
//!    `RCC_BDCR`, unique ID, flash size, RTC time and date, the backup
//!    registers, and raw blocks of the RCC, FLASH, PWR/TAMP, RTC and DBGMCU
//!    registers; then OTP and memory-mapped option bytes;
//! 2. the whole internal flash, page by page, then the ECC registers again;
//! 3. SRAM, only when asked for: tags idle in Shutdown or Standby, which do
//!    not keep SRAM, so the state that matters is in the backup registers;
//! 4. the external flash, through the loader the identity record names,
//!    last, because the loader overwrites the start of SRAM1.
//!
//! Nothing on the tag runs: the session holds the core at its reset vector
//! throughout. The result is one timestamped directory holding one file per
//! region and a `manifest.json`, in the same layout and the same JSON text
//! as the deleted C++ capture, so the C++ rebuild reads captures from
//! either, and the two were compared byte for byte before it went. A region that fails is
//! recorded and the capture continues.

use std::fmt::Write as _;
use std::path::{Path, PathBuf};
use std::time::Instant;

use log::error;

use super::build::{BuildFiles, BuildRecord};
use super::external::{
    ExternalCaptureOptions, ExternalCaptureResult, capture_external_flash, sha256_hex,
};
use super::identity::{IdentityRecord, parse_identity_record};
use super::image::TargetImage;
use super::mcu::McuMap;
use super::session::{AddressRange, AttachInfo, MON_EN, Session, SwdExit, VC_CORERESET};
use crate::link::UsbDev;

/// Upper bound on a believable flash-size register value, in KB.
const MAX_FLASH_KB: u32 = 4096;

/// What to capture and where.
pub struct CaptureOptions<'a> {
    /// Directory that receives `capture-YYYYmmdd-HHMMSS/` (UTC).
    pub parent_dir: PathBuf,
    /// Free text recorded in the manifest.
    pub reason: String,
    /// Also capture SRAM1 and SRAM2 (opt-in).
    pub include_sram: bool,
    /// Also capture the external flash, through the loader the identity
    /// record names (or [`loader_path`](Self::loader_path)).
    pub include_external: bool,
    /// Loader image to use; `None` finds it by the record's name.
    pub loader_path: Option<PathBuf>,
    /// Directories searched, recursively and in order, for `NAME.stldr`.
    pub loader_dirs: Vec<PathBuf>,
    /// Paged parts: read every block, blank or not.
    pub external_full: bool,
    /// How to leave the tag (used by [`capture_tag`]).
    pub exit: SwdExit,
    /// The firmware build to store under `build/`, resolved with
    /// [`BuildFiles::resolve`] before the capture; `None` stores nothing and
    /// leaves the manifest as the C++ capture wrote it.
    pub build: Option<BuildFiles>,
    /// Called with one line per progress update.
    pub progress: Option<&'a mut dyn FnMut(&str)>,
}

impl Default for CaptureOptions<'_> {
    fn default() -> Self {
        CaptureOptions {
            parent_dir: PathBuf::from("captures"),
            reason: String::new(),
            include_sram: false,
            include_external: true,
            loader_path: None,
            loader_dirs: Vec::new(),
            external_full: false,
            exit: SwdExit::HardwareReset,
            build: None,
            progress: None,
        }
    }
}

/// One region as captured.
#[derive(Debug, Clone, Default)]
pub struct CapturedRegion {
    /// e.g. `"internal_flash"`.
    pub name: String,
    /// File name within the capture directory.
    pub file: String,
    /// First byte.
    pub addr: u32,
    /// Bytes.
    pub size: u32,
    /// Every byte was read.
    pub ok: bool,
    /// SHA-256 of the file as written.
    pub sha256: String,
    /// Ranges left zero.
    pub failed: Vec<AddressRange>,
    /// Read time in seconds.
    pub seconds: f64,
}

/// A single register value recorded in the manifest.
#[derive(Debug, Clone, Default)]
pub struct NamedRegister {
    /// e.g. `"FLASH_OPTR"`.
    pub name: String,
    /// Register address.
    pub addr: u32,
    /// Value read; 0 when the read failed.
    pub value: u32,
    /// The read succeeded.
    pub ok: bool,
}

/// The outcome of a capture.
#[derive(Debug, Clone)]
pub struct CaptureResult {
    /// Capture directory.
    pub dir: PathBuf,
    /// Path of `manifest.json`.
    pub manifest: PathBuf,
    /// Part name.
    pub mcu: String,
    /// 96-bit unique ID, hex, in the monitor's `info.uuid` form.
    pub uid: String,
    /// What the session observed.
    pub attach: AttachInfo,
    /// Named register values.
    pub registers: Vec<NamedRegister>,
    /// Files written.
    pub regions: Vec<CapturedRegion>,
    /// The identity record parsed from internal flash, or why there is none.
    pub identity: Result<IdentityRecord, String>,
    /// External flash capture was tried.
    pub external_attempted: bool,
    /// Why it was skipped, or how it disagrees with the record.
    pub external_note: String,
    /// What it read.
    pub external: ExternalCaptureResult,
    /// What [`CaptureOptions::build`] stored, when one was given.
    pub build: Option<BuildRecord>,
    /// Every region was read in full.
    pub complete: bool,
}

impl CaptureResult {
    /// A register read successfully, by name.
    fn register(&self, name: &str) -> Option<&NamedRegister> {
        self.registers.iter().find(|r| r.name == name && r.ok)
    }
}

/// A UTC timestamp in `strftime` `format`.
fn utc_stamp(format: &str) -> String {
    chrono::Utc::now().format(format).to_string()
}

/// `0x%08X`.
fn hex32(v: u32) -> String {
    format!("0x{v:08X}")
}

/// A JSON string literal, escaped as the C++ capture escapes it.
fn json_string(s: &str) -> String {
    let mut out = String::from("\"");
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => {
                let _ = write!(out, "\\u{:04x}", c as u32);
            }
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

/// `true` or `false`.
fn jbool(b: bool) -> &'static str {
    if b { "true" } else { "false" }
}

/// Sends a progress line, if anyone is listening.
fn progress(o: &mut CaptureOptions, line: &str) {
    if let Some(p) = o.progress.as_deref_mut() {
        p(line);
    }
}

/// Reads one register into a [`NamedRegister`].
fn read_named(s: &mut Session, name: &str, addr: u32) -> NamedRegister {
    let v = s.read_word(addr);
    NamedRegister { name: name.into(), addr, value: v.unwrap_or(0), ok: v.is_some() }
}

/// Reads one region into `dir/<name>.bin` and records it.
fn capture_region(
    s: &mut Session,
    dir: &Path,
    o: &mut CaptureOptions,
    name: &str,
    addr: u32,
    size: u32,
    chunk: u32,
) -> CapturedRegion {
    let mut r = CapturedRegion {
        name: name.into(),
        file: format!("{name}.bin"),
        addr,
        size,
        ..Default::default()
    };
    progress(o, &format!("reading {name} ({size} bytes at {})", hex32(addr)));
    let mut data = vec![0u8; size as usize];
    let start = Instant::now();
    r.ok = s.read(addr, &mut data, chunk, Some(&mut r.failed));
    r.seconds = start.elapsed().as_secs_f64();
    let path = dir.join(&r.file);
    if let Err(e) = std::fs::write(&path, &data) {
        error!("could not write {}: {e}", path.display());
        r.ok = false;
    }
    r.sha256 = sha256_hex(&data);
    r
}

/// Option-byte fields that change what the capture means.
fn decode_options(m: &McuMap, optr: u32) -> String {
    let rdp = optr & 0xFF;
    let iwdg_sw = (optr >> 16) & 1;
    let sram2_kept = (optr >> 25) & 1;
    // Integers, not true/false: the C++ capture streams these bools bare.
    let mut j = format!(
        "{{\"flash_optr\": {}, \"rdp\": {}, \"rdp_level0\": {}, \"iwdg_sw\": {iwdg_sw}, \
         \"sram2_rst\": {sram2_kept}",
        json_string(&hex32(optr)),
        json_string(&hex32(rdp)),
        jbool(rdp == 0xAA)
    );
    let mut erased = Vec::new();
    if sram2_kept == 0 {
        erased.push("sram2");
    }
    if !m.is_l4() {
        // U3 bit 15 is SRAM_RST or SRAM1_RST depending on the part variant;
        // both cover SRAM1.
        let sram1_kept = (optr >> 15) & 1;
        let _ = write!(j, ", \"sram1_rst\": {sram1_kept}");
        if sram1_kept == 0 {
            erased.insert(0, "sram1");
        }
    }
    let list: Vec<String> = erased.iter().map(|e| json_string(e)).collect();
    let _ = write!(j, ", \"sram_erased_by_attach\": [{}]}}", list.join(", "));
    j
}

/// `RCC_CSR` reset flags; decoded for the STM32L4 only, raw otherwise.
fn decode_reset_flags(m: &McuMap, csr: u32) -> String {
    const NAMES: [&str; 8] = [
        "FWRSTF", "OBLRSTF", "PINRSTF", "BORRSTF", "SFTRSTF", "IWDGRSTF", "WWDGRSTF",
        "LPWRRSTF",
    ];
    let flags: Vec<String> = if m.is_l4() {
        (24..32)
            .filter(|bit| csr & (1 << bit) != 0)
            .map(|bit| json_string(NAMES[bit - 24]))
            .collect()
    } else {
        Vec::new()
    };
    format!(
        "{{\"rcc_csr\": {}, \"flags\": [{}], \"note\": \"PINRSTF includes the capture's own attach\"}}",
        json_string(&hex32(csr)),
        flags.join(", ")
    )
}

/// The manifest's `identity` object.
fn identity_json(id: &Result<IdentityRecord, String>) -> String {
    let id = match id {
        Ok(id) => id,
        Err(e) => return format!("{{\"found\": false, \"error\": {}}}", json_string(e)),
    };
    let mut j = format!("{{\"found\": true, \"format_version\": {}", id.format_version);
    for k in ["target", "family", "board", "loader", "decoder", "git_sha", "flash_part"] {
        let _ = write!(j, ", {}: {}", json_string(k), json_string(id.string(k)));
    }
    if let Some(x) = id.external_flash {
        let _ = write!(
            j,
            ", \"external_jedec\": {}, \"external_size\": {}, \"external_page\": {}, \
             \"external_spare\": {}",
            json_string(&hex32(x.jedec_id)),
            x.flash_size,
            x.program_page,
            x.spare
        );
    }
    j.push('}');
    j
}

/// The manifest's `external_flash` object.
fn external_json(r: &CaptureResult) -> String {
    if !r.external_attempted {
        return format!("{{\"captured\": false, \"note\": {}}}", json_string(&r.external_note));
    }
    let x = &r.external;
    let mut j = format!(
        "{{\"captured\": {}, \"error\": {}, \"note\": {}, \"loader\": {}, \"loader_sha256\": {}, \
         \"serve_version\": {}, \"jedec\": {}, \"status_as_found\": {}, \"size\": {}, \"seconds\": {:.1}",
        jbool(x.ok),
        json_string(&x.error),
        json_string(&r.external_note),
        json_string(&x.loader_path),
        json_string(&x.loader_sha256),
        x.version,
        json_string(&hex32(x.jedec)),
        json_string(&hex32(x.sr1)),
        x.size,
        x.seconds
    );
    if x.paged {
        let _ = write!(
            j,
            ", \"paged\": true, \"found\": {{\"A0\": {}, \"B0\": {}, \"C0\": {}, \"F0\": {}}}, \
             \"page_bytes\": {}, \"pages_per_block\": {}, \"blocks_scanned\": {}, \
             \"blocks_read\": {}, \"blocks_blank\": {}, \"blocks_factory_marked\": {}, \
             \"pages_uncorrectable\": {}, \"selection\": {}",
            x.found & 0xFF,
            (x.found >> 8) & 0xFF,
            (x.found >> 16) & 0xFF,
            x.found >> 24,
            x.page_bytes,
            x.pages_per_block,
            x.blocks_scanned,
            x.blocks_read,
            x.blocks_blank,
            x.blocks_marked,
            x.pages_uncorrectable,
            json_string("page 0 of each block read raw; blank blocks skipped")
        );
    }
    let files: Vec<String> = x
        .files
        .iter()
        .map(|f| {
            format!(
                "{{\"name\": {}, \"file\": {}, \"size\": {}, \"sha256\": {}}}",
                json_string(&f.name),
                json_string(&f.file),
                f.size,
                json_string(&f.sha256)
            )
        })
        .collect();
    let _ = write!(j, ", \"files\": [{}]}}", files.join(", "));
    j
}

/// The first `NAME.stldr` under `dirs`, searched recursively in order
/// (entries of a directory in name order), or `None`.
pub fn find_loader(name: &str, dirs: &[PathBuf]) -> Option<PathBuf> {
    /// Searches one directory tree.
    fn walk(dir: &Path, want: &str) -> Option<PathBuf> {
        let mut entries: Vec<PathBuf> = std::fs::read_dir(dir)
            .ok()?
            .filter_map(|e| e.ok().map(|e| e.path()))
            .collect();
        entries.sort();
        for p in &entries {
            if p.is_file() && p.file_name().is_some_and(|f| f == want) {
                return Some(p.clone());
            }
        }
        entries.iter().filter(|p| p.is_dir()).find_map(|p| walk(p, want))
    }
    let want = format!("{name}.stldr");
    dirs.iter().filter(|d| d.is_dir()).find_map(|d| walk(d, &want))
}

/// The whole `manifest.json` text.
fn build_manifest(
    o: &CaptureOptions,
    r: &CaptureResult,
    m: &McuMap,
    flash_kb: u32,
    clock_notes: &[String],
) -> String {
    let a = &r.attach;
    let mut j = String::from("{\n");
    j += "  \"format\": \"tag-capture\",\n  \"format_version\": 1,\n";
    let _ = writeln!(j, "  \"captured_at\": {},", json_string(&utc_stamp("%Y-%m-%dT%H:%M:%SZ")));
    let _ = writeln!(j, "  \"reason\": {},", json_string(&o.reason));
    j += "  \"capture_order\": [\"registers\", \"info_regions\", \"internal_flash\", \
          \"ecc_after\", \"sram\", \"external_flash\"],\n";
    let _ = writeln!(j, "  \"identity\": {},", identity_json(&r.identity));
    let _ = writeln!(j, "  \"external_flash\": {},", external_json(r));
    let _ = writeln!(
        j,
        "  \"target\": {{\"mcu\": {}, \"idcode\": {}, \"uid\": {}, \"flash_kb\": {flash_kb}, \
         \"voltage\": {:.2}}},",
        json_string(&r.mcu),
        json_string(&hex32(a.idcode)),
        json_string(&r.uid),
        a.voltage
    );
    let _ = writeln!(
        j,
        "  \"attach\": {{\"dhcsr_before\": {}, \"demcr_before\": {}, \"monitor_left_attached\": {}, \
         \"dbgmcu_apb1fzr1_before\": {}, \"halted\": {}, \"pc\": {}, \"reset_vector\": {}, \
         \"at_reset_vector\": {}, \"flash_blank\": {}}},",
        json_string(&hex32(a.dhcsr_before)),
        json_string(&hex32(a.demcr_before)),
        jbool(a.demcr_before & (MON_EN | VC_CORERESET) != 0),
        json_string(&hex32(a.fz_before)),
        jbool(a.halted),
        json_string(&hex32(a.pc)),
        json_string(&hex32(a.reset_vector)),
        jbool(a.at_reset_vector),
        jbool(a.flash_blank)
    );
    if let Some(optr) = r.register("FLASH_OPTR") {
        let _ = writeln!(j, "  \"option_bytes\": {},", decode_options(m, optr.value));
    }
    if let Some(csr) = r.register("RCC_CSR") {
        let _ = writeln!(j, "  \"reset_flags\": {},", decode_reset_flags(m, csr.value));
    }
    let notes: Vec<String> = clock_notes.iter().map(|n| json_string(n)).collect();
    let _ = writeln!(j, "  \"clock_enables_set\": [{}],", notes.join(", "));

    j += "  \"registers\": [\n";
    for (i, reg) in r.registers.iter().enumerate() {
        let _ = write!(
            j,
            "    {{\"name\": {}, \"addr\": {}, \"value\": {}, \"ok\": {}}}{}",
            json_string(&reg.name),
            json_string(&hex32(reg.addr)),
            json_string(&hex32(reg.value)),
            jbool(reg.ok),
            if i + 1 < r.registers.len() { ",\n" } else { "\n" }
        );
    }
    j += "  ],\n";

    j += "  \"regions\": [\n";
    for (i, g) in r.regions.iter().enumerate() {
        let rate = if g.seconds > 0.0 { f64::from(g.size) / g.seconds } else { 0.0 };
        let failed: Vec<String> = g
            .failed
            .iter()
            .map(|(addr, len)| format!("[{}, {len}]", json_string(&hex32(*addr))))
            .collect();
        let _ = write!(
            j,
            "    {{\"name\": {}, \"file\": {}, \"addr\": {}, \"size\": {}, \"ok\": {}, \"sha256\": {}, \
             \"seconds\": {:.3}, \"bytes_per_second\": {rate:.0}, \"failed\": [{}]}}{}",
            json_string(&g.name),
            json_string(&g.file),
            json_string(&hex32(g.addr)),
            g.size,
            jbool(g.ok),
            json_string(&g.sha256),
            g.seconds,
            failed.join(", "),
            if i + 1 < r.regions.len() { ",\n" } else { "\n" }
        );
    }
    j += "  ],\n";
    if let Some(b) = &r.build {
        let _ = writeln!(j, "  \"build\": {},", b.to_json(&json_string));
    }
    let _ = writeln!(j, "  \"complete\": {},", jbool(r.complete));
    let _ = writeln!(j, "  \"exit\": {}", json_string(o.exit.manifest_name()));
    j += "}\n";
    j
}

/// Captures through an already open session, which is left open.
///
/// Returns the result once the directory and manifest are written, even if
/// some regions failed (see [`CaptureResult::complete`]).
///
/// # Errors
///
/// The session is not open, the capture directory could not be created, or
/// the manifest could not be written.
pub fn capture_state(s: &mut Session, o: &mut CaptureOptions) -> Result<CaptureResult, String> {
    let m = s.mcu().filter(|_| s.is_open()).ok_or("session not open")?;
    let mut r = CaptureResult {
        dir: PathBuf::new(),
        manifest: PathBuf::new(),
        mcu: m.name.into(),
        uid: String::new(),
        attach: *s.attach_info(),
        registers: Vec::new(),
        regions: Vec::new(),
        identity: Err(String::new()), // as the C++ capture: no reason when flash was not read
        external_attempted: false,
        external_note: String::new(),
        external: ExternalCaptureResult::default(),
        build: None,
        complete: false,
    };

    let dir = o.parent_dir.join(format!("capture-{}", utc_stamp("%Y%m%d-%H%M%S")));
    std::fs::create_dir_all(&dir).map_err(|e| format!("could not create {}: {e}", dir.display()))?;
    r.dir = dir.clone();

    // 1. Registers. Option bytes and ECC first: the ECC registers may hold
    //    the tag's own last fault, and reading flash can overwrite them.
    progress(o, "reading registers");
    r.registers.push(read_named(s, "FLASH_OPTR", m.flash_optr));
    for (i, &reg) in m.ecc_regs.iter().enumerate() {
        r.registers.push(read_named(s, &format!("FLASH_ECC{i}_before"), reg));
    }
    r.registers.push(read_named(s, "RCC_CSR", m.rcc_csr));
    r.registers.push(read_named(s, "RCC_BDCR", m.rcc_bdcr));
    r.registers.push(read_named(s, "UID0", m.uid));
    r.registers.push(read_named(s, "UID1", m.uid + 4));
    r.registers.push(read_named(s, "UID2", m.uid + 8));
    r.registers.push(read_named(s, "FLASH_SIZE", m.flashsize_reg));

    // The raw RCC block is read before any clock enable is changed.
    for blk in m.register_blocks.iter().filter(|b| b.name == "rcc") {
        let g = capture_region(s, &dir, o, blk.name, blk.addr, blk.size, 512);
        r.regions.push(g);
    }

    // Backup registers and RTC need their APB clock; on L4 PWR does too. The
    // attach reset RCC, so these enables only undo that reset, and the tag's
    // own boot resets them again.
    let mut clock_notes = Vec::new();
    if let Some(enr) = s.read_word(m.rcc_apb1enr1) {
        let want = enr | m.rtcapben | m.pwren;
        if want != enr && s.write_word(m.rcc_apb1enr1, want) {
            clock_notes.push(format!("RCC_APB1ENR1 {} -> {}", hex32(enr), hex32(want)));
        }
    }

    r.registers.push(read_named(s, "RTC_TR", m.rtc_tr));
    r.registers.push(read_named(s, "RTC_DR", m.rtc_dr));
    for blk in m.register_blocks.iter().filter(|b| b.name != "rcc") {
        let g = capture_region(s, &dir, o, blk.name, blk.addr, blk.size, 512);
        r.regions.push(g);
    }
    let g = capture_region(s, &dir, o, "backup_regs", m.backup_regs, m.backup_count * 4, 512);
    r.regions.push(g);
    for info in m.info_regions {
        let g = capture_region(s, &dir, o, info.name, info.addr, info.size, 512);
        r.regions.push(g);
    }

    if let (Some(u0), Some(u1), Some(u2)) =
        (r.register("UID0"), r.register("UID1"), r.register("UID2"))
    {
        // The same form as the monitor's info.uuid.
        r.uid = format!("{:08X}{:08X}{:08X}", u2.value, u1.value, u0.value);
    }

    // 2. Internal flash, sized by the part's own flash-size register.
    let flash_kb = r.register("FLASH_SIZE").map_or(0, |f| f.value & 0xFFFF);
    if flash_kb == 0 || flash_kb > MAX_FLASH_KB {
        error!("implausible flash size register {flash_kb} KB; internal flash skipped");
    } else {
        let g = capture_region(s, &dir, o, "internal_flash", m.flash_base, flash_kb * 1024, m.flash_page);
        r.regions.push(g);
    }
    for (i, &reg) in m.ecc_regs.iter().enumerate() {
        r.registers.push(read_named(s, &format!("FLASH_ECC{i}_after"), reg));
    }

    // 3. SRAM.
    if o.include_sram {
        for sram in m.sram {
            let g = capture_region(s, &dir, o, sram.name, sram.addr, sram.size, 4096);
            r.regions.push(g);
        }
    }

    // 4. External flash, last: the loader overwrites the start of SRAM1.
    if flash_kb != 0 {
        let image = std::fs::read(dir.join("internal_flash.bin")).unwrap_or_default();
        r.identity = parse_identity_record(&image, m.identity_offset);
    }
    let named_loader = r.identity.as_ref().map(|id| id.string("loader").to_string()).unwrap_or_default();
    let record_says_none = r.identity.as_ref().is_ok_and(|id| id.has_no_external_flash());
    if !o.include_external {
        r.external_note = "not requested".into();
    } else {
        let mut loader = o.loader_path.clone();
        if loader.is_none() {
            if r.identity.is_err() {
                r.external_note = "no identity record, so no loader named; pass --loader".into();
            } else if named_loader.is_empty() && record_says_none {
                r.external_note = "the tag has no external flash".into();
            } else if named_loader.is_empty() {
                r.external_note = "the identity record names no loader".into();
            } else {
                loader = find_loader(&named_loader, &o.loader_dirs);
                if loader.is_none() {
                    r.external_note =
                        format!("loader {named_loader}.stldr not found in the loader directories");
                }
            }
        }
        let image = match loader.as_deref().map(TargetImage::load) {
            Some(Ok(image)) => Some(image),
            Some(Err(e)) => {
                r.external_note = e;
                None
            }
            None => None,
        };
        if let Some(image) = image {
            progress(o, &format!("reading external flash through {}", image.path().display()));
            let full = o.external_full;
            let mut forward = |line: &str| progress(o, line);
            let mut xo = ExternalCaptureOptions {
                prefix: "external_".into(),
                full,
                progress: Some(&mut forward),
                ..Default::default()
            };
            r.external_attempted = true;
            r.external = capture_external_flash(s, &image, &dir, &mut xo);
            // The firmware knows the manufacturer and first device byte.
            if let Ok(id) = &r.identity
                && let Some(x) = id.external_flash
                && r.external.ok
            {
                let (want, got) = (x.jedec_id, r.external.jedec);
                if (want >> 16) != (got >> 16) || (want & 0xFF) != ((got >> 8) & 0xFF) {
                    r.external_note = format!(
                        "JEDEC {} does not match the record's {}",
                        hex32(got),
                        hex32(want)
                    );
                }
            }
        }
    }

    r.complete = flash_kb != 0;
    // A tag whose identity record declares no external flash (BitTag) has
    // nothing to read there, so the stage cannot make the capture incomplete.
    let no_external_flash = record_says_none && named_loader.is_empty() && o.loader_path.is_none();
    if o.include_external && !no_external_flash {
        r.complete = r.complete && r.external_attempted && r.external.ok;
    }
    r.complete = r.complete && r.registers.iter().all(|g| g.ok) && r.regions.iter().all(|g| g.ok);

    // The build last: it needs nothing from the tag, only the flash file.
    if let Some(build) = o.build.clone() {
        progress(o, &format!("storing the build from {}", build.source_dir.display()));
        let flash = r
            .regions
            .iter()
            .find(|g| g.name == "internal_flash" && g.ok)
            .and_then(|g| std::fs::read(dir.join(&g.file)).ok().map(|d| (d, g.addr)));
        r.build = Some(build.store(&dir, flash.as_ref().map(|(d, a)| (d.as_slice(), *a))));
    }

    let manifest = dir.join("manifest.json");
    std::fs::write(&manifest, build_manifest(o, &r, m, flash_kb, &clock_notes))
        .map_err(|e| format!("could not write {}: {e}", manifest.display()))?;
    r.manifest = manifest;
    Ok(r)
}

/// Opens a session on `usbdev`, captures, and closes it as `o.exit` says.
///
/// # Errors
///
/// "could not open an SWD session halted at reset: ..." with the reason,
/// and the attach as observed; or [`capture_state`]'s errors.
pub fn capture_tag(
    o: &mut CaptureOptions,
    usbdev: &UsbDev,
) -> Result<CaptureResult, (String, AttachInfo)> {
    let mut s = Session::new();
    if let Err(e) = s.open(usbdev) {
        return Err((
            format!("could not open an SWD session halted at reset: {e}"),
            *s.attach_info(),
        ));
    }
    let r = capture_state(&mut s, o);
    s.close(o.exit);
    r.map_err(|e| (e, *s.attach_info()))
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Creates empty files at `paths` under a fresh temporary directory and
    /// returns it.
    fn tree(name: &str, paths: &[&str]) -> PathBuf {
        let root = std::env::temp_dir().join(format!("tagcore-find-loader-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        for p in paths {
            let f = root.join(p);
            std::fs::create_dir_all(f.parent().unwrap()).unwrap();
            std::fs::write(f, b"").unwrap();
        }
        root
    }

    /// The firmware release archive (`firmware/loaders/<image>/`) and the
    /// build tree (`embedded/loaders/<dir>/<image>/build/`) both resolve,
    /// and the read-only image is never confused with its `-RW` sibling.
    #[test]
    fn find_loader_in_release_and_build_layouts() {
        let release = tree(
            "release",
            &[
                "firmware/PresTag/PresTag.elf",
                "firmware/loaders/AT25XE_PresTagv3-RW/AT25XE_PresTagv3-RW.stldr",
                "firmware/loaders/AT25XE_PresTagv3/AT25XE_PresTagv3.stldr",
            ],
        );
        let found = find_loader("AT25XE_PresTagv3", &[release.join("firmware")]).unwrap();
        assert!(found.ends_with("loaders/AT25XE_PresTagv3/AT25XE_PresTagv3.stldr"));

        let build = tree(
            "build",
            &["embedded/loaders/AT25XE_PresTagv3/AT25XE_PresTagv3/build/AT25XE_PresTagv3.stldr"],
        );
        // Searched in order: a missing directory is skipped, the first hit wins.
        let dirs = [release.join("missing"), build.clone(), release.clone()];
        let found = find_loader("AT25XE_PresTagv3", &dirs).unwrap();
        assert!(found.starts_with(&build));
        assert!(find_loader("GD5F2GM7RE_IMUTagNandv2", &dirs).is_none());

        let _ = std::fs::remove_dir_all(release);
        let _ = std::fs::remove_dir_all(build);
    }
}
