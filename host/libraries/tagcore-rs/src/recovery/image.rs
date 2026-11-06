//! The loadable segments and symbols of an ELF32 ARM executable.
//!
//! Port of `tagcore/recovery/targetimage.cc` on the `object` crate. A
//! loader or probe image (`.stldr` or `.elf`) is downloaded by its PT_LOAD
//! segments and called by symbol name, so that is all this reads.

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use object::elf::{EM_ARM, FileHeader32, PT_LOAD};
use object::read::elf::{FileHeader, ProgramHeader, Sym};
use object::Endianness;

/// One PT_LOAD segment with a non-zero memory size.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ImageSegment {
    /// Load (physical) address.
    pub addr: u32,
    /// Size in memory; bytes past `data.len()` are zero (`.bss`).
    pub memsz: u32,
    /// File contents, `p_filesz` bytes.
    pub data: Vec<u8>,
}

/// An ELF image read for download. Plain data once loaded.
#[derive(Debug, Clone)]
pub struct TargetImage {
    /// The file it was read from.
    path: PathBuf,
    /// The PT_LOAD segments with a non-zero memory size, in file order.
    segments: Vec<ImageSegment>,
    /// Symbol values by name; the first symbol of a name wins, as the C++
    /// reader's `std::map::emplace` keeps it.
    symbols: HashMap<String, u32>,
}

impl TargetImage {
    /// Reads an ELF32 little-endian ARM executable.
    ///
    /// # Errors
    ///
    /// The file cannot be read; it is not ELF32 little-endian ARM; a
    /// program header, segment or symbol table lies outside the file; or the
    /// image has no symbols at all, which is refused because every caller
    /// finds its entry points by name.
    pub fn load(path: &Path) -> Result<TargetImage, String> {
        let name = path.display();
        let data = std::fs::read(path).map_err(|e| format!("cannot open {name}: {e}"))?;
        Self::parse(&data, path).map_err(|e| format!("{name}{e}"))
    }

    /// [`load`](Self::load) from bytes already read; `path` is recorded as
    /// the image's name. Errors are suffixes for the caller to prefix with
    /// the path (`" is not an ELF file"`, `": segment out of range"`).
    fn parse(data: &[u8], path: &Path) -> Result<TargetImage, String> {
        if data.len() < 52 || &data[0..4] != b"\x7fELF" {
            return Err(" is not an ELF file".into());
        }
        if data[4] != 1 || data[5] != 1 {
            return Err(" is not ELF32 little-endian".into());
        }
        let header = FileHeader32::<Endianness>::parse(data).map_err(|e| format!(": {e}"))?;
        let endian = header.endian().map_err(|e| format!(": {e}"))?;
        if header.e_machine(endian) != EM_ARM {
            return Err(" is not an ARM image".into());
        }

        let mut segments = Vec::new();
        for ph in header
            .program_headers(endian, data)
            .map_err(|_| ": program headers out of range".to_string())?
        {
            if ph.p_type(endian) != PT_LOAD || ph.p_memsz(endian) == 0 {
                continue;
            }
            let memsz = ph.p_memsz(endian);
            let bytes = ph
                .data(endian, data)
                .map_err(|_| ": segment out of range".to_string())?;
            if bytes.len() as u64 > u64::from(memsz) {
                return Err(": segment out of range".into());
            }
            segments.push(ImageSegment {
                addr: ph.p_paddr(endian),
                memsz,
                data: bytes.to_vec(),
            });
        }

        let sections = header
            .sections(endian, data)
            .map_err(|_| ": section headers out of range".to_string())?;
        let mut symbols = HashMap::new();
        let table = sections
            .symbols(endian, data, object::elf::SHT_SYMTAB)
            .map_err(|_| ": malformed symbol table".to_string())?;
        for sym in table.iter() {
            let Ok(sym_name) = sym.name(endian, table.strings()) else {
                continue;
            };
            if sym_name.is_empty() {
                continue;
            }
            let key = String::from_utf8_lossy(sym_name).into_owned();
            symbols.entry(key).or_insert(sym.st_value(endian));
        }
        if symbols.is_empty() {
            return Err(" has no symbol table".into());
        }
        Ok(TargetImage {
            path: path.to_path_buf(),
            segments,
            symbols,
        })
    }

    /// An image of the given `(address, data)` segments and no symbols,
    /// for tests of code that walks segments.
    #[cfg(test)]
    pub(crate) fn from_segments_for_test(segments: Vec<(u32, Vec<u8>)>) -> TargetImage {
        TargetImage {
            path: PathBuf::from("test.elf"),
            segments: segments
                .into_iter()
                .map(|(addr, data)| ImageSegment { addr, memsz: data.len() as u32, data })
                .collect(),
            symbols: HashMap::new(),
        }
    }

    /// The PT_LOAD segments with a non-zero memory size, in file order.
    pub fn segments(&self) -> &[ImageSegment] {
        &self.segments
    }

    /// A symbol's value. For a Thumb function this carries bit 0.
    pub fn symbol(&self, name: &str) -> Option<u32> {
        self.symbols.get(name).copied()
    }

    /// `len` bytes at load address `addr`, if one segment's file data covers
    /// the whole range.
    pub fn read_at(&self, addr: u32, len: usize) -> Option<&[u8]> {
        self.segments.iter().find_map(|s| {
            let start = addr.checked_sub(s.addr)? as usize;
            s.data.get(start..start.checked_add(len)?)
        })
    }

    /// The file this image was read from.
    pub fn path(&self) -> &Path {
        &self.path
    }
}

/// Unit tests of the reader on a hand-built ELF.
#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    /// Builds a minimal ELF32 ARM executable with one PT_LOAD segment
    /// (`addr`, `data`, `memsz`) and the given symbols.
    pub(crate) fn build_elf(addr: u32, data: &[u8], memsz: u32, syms: &[(&str, u32)]) -> Vec<u8> {
        let mut strtab = vec![0u8];
        let mut symtab = vec![0u8; 16];
        for (name, value) in syms {
            let off = strtab.len() as u32;
            strtab.extend_from_slice(name.as_bytes());
            strtab.push(0);
            let mut e = [0u8; 16];
            e[0..4].copy_from_slice(&off.to_le_bytes());
            e[4..8].copy_from_slice(&value.to_le_bytes());
            e[12] = 0x12; // global function
            e[14..16].copy_from_slice(&1u16.to_le_bytes());
            symtab.extend_from_slice(&e);
        }
        let shstr = b"\0.text\0.symtab\0.strtab\0.shstrtab\0".to_vec();
        let ph_off = 52usize;
        let data_off = ph_off + 32;
        let sym_off = data_off + data.len().div_ceil(4) * 4;
        let str_off = sym_off + symtab.len();
        let shs_off = str_off + strtab.len();
        let sh_off = (shs_off + shstr.len()).div_ceil(4) * 4;
        let mut f = vec![0u8; sh_off + 5 * 40];
        f[0..4].copy_from_slice(b"\x7fELF");
        f[4] = 1;
        f[5] = 1;
        f[6] = 1;
        let put16 = |f: &mut Vec<u8>, o: usize, v: u16| f[o..o + 2].copy_from_slice(&v.to_le_bytes());
        let put32 = |f: &mut Vec<u8>, o: usize, v: u32| f[o..o + 4].copy_from_slice(&v.to_le_bytes());
        put16(&mut f, 16, 2);
        put16(&mut f, 18, 40);
        put32(&mut f, 20, 1);
        put32(&mut f, 28, ph_off as u32);
        put32(&mut f, 32, sh_off as u32);
        put16(&mut f, 40, 52);
        put16(&mut f, 42, 32);
        put16(&mut f, 44, 1);
        put16(&mut f, 46, 40);
        put16(&mut f, 48, 5);
        put16(&mut f, 50, 4);
        // Program header.
        put32(&mut f, ph_off, 1);
        put32(&mut f, ph_off + 4, data_off as u32);
        put32(&mut f, ph_off + 8, addr);
        put32(&mut f, ph_off + 12, addr);
        put32(&mut f, ph_off + 16, data.len() as u32);
        put32(&mut f, ph_off + 20, memsz);
        f[data_off..data_off + data.len()].copy_from_slice(data);
        f[sym_off..sym_off + symtab.len()].copy_from_slice(&symtab);
        f[str_off..str_off + strtab.len()].copy_from_slice(&strtab);
        f[shs_off..shs_off + shstr.len()].copy_from_slice(&shstr);
        // Section headers: null, .text, .symtab, .strtab, .shstrtab.
        let sh = |f: &mut Vec<u8>, i: usize, name: u32, ty: u32, off: usize, size: usize, link: u32, ent: u32| {
            let o = sh_off + i * 40;
            f[o..o + 4].copy_from_slice(&name.to_le_bytes());
            f[o + 4..o + 8].copy_from_slice(&ty.to_le_bytes());
            f[o + 16..o + 20].copy_from_slice(&(off as u32).to_le_bytes());
            f[o + 20..o + 24].copy_from_slice(&(size as u32).to_le_bytes());
            f[o + 24..o + 28].copy_from_slice(&link.to_le_bytes());
            f[o + 36..o + 40].copy_from_slice(&ent.to_le_bytes());
        };
        sh(&mut f, 1, 1, 1, data_off, data.len(), 0, 0);
        sh(&mut f, 2, 7, 2, sym_off, symtab.len(), 3, 16);
        sh(&mut f, 3, 15, 3, str_off, strtab.len(), 0, 0);
        sh(&mut f, 4, 23, 3, shs_off, shstr.len(), 0, 0);
        f
    }

    /// Segments, symbols and `read_at` come back as built.
    #[test]
    fn reads_segments_and_symbols() {
        let elf = build_elf(0x2000_0100, &[1, 2, 3, 4, 5, 6, 7, 8], 16, &[("Serve", 0x2000_0105)]);
        let img = TargetImage::parse(&elf, Path::new("t.elf")).unwrap();
        assert_eq!(img.segments().len(), 1);
        assert_eq!(img.segments()[0].addr, 0x2000_0100);
        assert_eq!(img.segments()[0].memsz, 16);
        assert_eq!(img.symbol("Serve"), Some(0x2000_0105));
        assert_eq!(img.symbol("Init"), None);
        assert_eq!(img.read_at(0x2000_0102, 4), Some(&[3u8, 4, 5, 6][..]));
        assert_eq!(img.read_at(0x2000_0106, 4), None);
    }

    /// An image with no symbols is refused.
    #[test]
    fn refuses_no_symbols() {
        let elf = build_elf(0x2000_0000, &[0; 4], 4, &[]);
        let err = TargetImage::parse(&elf, Path::new("t.elf")).unwrap_err();
        assert!(err.contains("no symbol table"), "{err}");
    }
}
