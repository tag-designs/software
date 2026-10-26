//! Minimal ELF32 little-endian reader for firmware images.
//!
//! Two readers, as in qtprogram:
//!
//! - [`Elf`] ports `tagcore::recovery::TargetImage`: an ARM image's loadable
//!   segments, at their *physical* (load) address, and its symbol table. Used
//!   for the identity record, the flash range and the read-back comparison.
//! - [`plan_symbols`] ports `flashplan.cpp`'s lenient symbol walk, with its
//!   own error messages, which the flash plan quotes.
//!
//! No relocation, no debug information, no external dependency.

use std::collections::HashMap;
use std::path::Path;

const EM_ARM: u16 = 40;
const PT_LOAD: u32 = 1;
const SHT_SYMTAB: u32 = 2;

fn u16_at(b: &[u8], o: usize) -> u16 {
    u16::from_le_bytes([b[o], b[o + 1]])
}

fn u32_at(b: &[u8], o: usize) -> u32 {
    u32::from_le_bytes([b[o], b[o + 1], b[o + 2], b[o + 3]])
}

/// One `PT_LOAD` segment with a non-zero memory size.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Segment {
    /// Physical (load) address: where the bytes go in flash.
    pub addr: u32,
    /// Size in memory; at least `data.len()`.
    pub memsz: u32,
    /// The file bytes (`p_filesz` of them); empty for NOLOAD segments.
    pub data: Vec<u8>,
}

/// An ARM ELF32 image's loadable segments and symbols.
#[derive(Debug, Clone, Default)]
pub struct Elf {
    /// Loadable segments in program-header order.
    pub segments: Vec<Segment>,
    symbols: HashMap<String, u32>,
}

impl Elf {
    /// Loads and parses the file at `path`.
    ///
    /// Errors name the path, as `TargetImage::Load` does: not an ELF, not
    /// ELF32 little-endian, not ARM, headers or tables out of range, or no
    /// symbol table.
    pub fn load(path: &Path) -> Result<Elf, String> {
        let bytes = std::fs::read(path).map_err(|_| format!("cannot open {}", path.display()))?;
        Elf::parse(&bytes, &path.display().to_string())
    }

    /// Parses `f`; `path` only names the file in error messages.
    pub fn parse(f: &[u8], path: &str) -> Result<Elf, String> {
        if f.len() < 52 || &f[0..4] != b"\x7fELF" {
            return Err(format!("{path} is not an ELF file"));
        }
        if f[4] != 1 || f[5] != 1 {
            return Err(format!("{path} is not ELF32 little-endian"));
        }
        if u16_at(f, 18) != EM_ARM {
            return Err(format!("{path} is not an ARM image"));
        }
        let in_file = |off: u64, len: u64| off + len <= f.len() as u64;

        let phoff = u32_at(f, 28) as usize;
        let shoff = u32_at(f, 32) as usize;
        let phentsize = u16_at(f, 42) as usize;
        let phnum = u16_at(f, 44) as usize;
        let shentsize = u16_at(f, 46) as usize;
        let shnum = u16_at(f, 48) as usize;

        if !in_file(phoff as u64, (phentsize * phnum) as u64) || phentsize < 32 {
            return Err(format!("{path}: program headers out of range"));
        }
        let mut segments = Vec::new();
        for i in 0..phnum {
            let p = phoff + i * phentsize;
            if u32_at(f, p) != PT_LOAD {
                continue;
            }
            let offset = u32_at(f, p + 4) as usize;
            let paddr = u32_at(f, p + 12);
            let filesz = u32_at(f, p + 16) as usize;
            let memsz = u32_at(f, p + 20);
            if memsz == 0 {
                continue;
            }
            if !in_file(offset as u64, filesz as u64) || filesz as u64 > u64::from(memsz) {
                return Err(format!("{path}: segment out of range"));
            }
            segments.push(Segment {
                addr: paddr,
                memsz,
                data: f[offset..offset + filesz].to_vec(),
            });
        }

        if !in_file(shoff as u64, (shentsize * shnum) as u64) || shentsize < 40 {
            return Err(format!("{path}: section headers out of range"));
        }
        let mut symbols = HashMap::new();
        for i in 0..shnum {
            let s = shoff + i * shentsize;
            if u32_at(f, s + 4) != SHT_SYMTAB {
                continue;
            }
            let off = u32_at(f, s + 16) as usize;
            let size = u32_at(f, s + 20) as usize;
            let link = u32_at(f, s + 24) as usize;
            let entsize = u32_at(f, s + 36) as usize;
            if entsize < 16 || link >= shnum || !in_file(off as u64, size as u64) {
                return Err(format!("{path}: malformed symbol table"));
            }
            let strh = shoff + link * shentsize;
            let stroff = u32_at(f, strh + 16) as usize;
            let strsize = u32_at(f, strh + 20) as usize;
            if !in_file(stroff as u64, strsize as u64) {
                return Err(format!("{path}: malformed string table"));
            }
            let mut e = 0;
            while e + entsize <= size {
                let name = u32_at(f, off + e) as usize;
                let value = u32_at(f, off + e + 4);
                e += entsize;
                if name == 0 || name >= strsize {
                    continue;
                }
                let text = &f[stroff + name..stroff + strsize];
                let end = text.iter().position(|&b| b == 0).unwrap_or(text.len());
                symbols
                    .entry(String::from_utf8_lossy(&text[..end]).into_owned())
                    .or_insert(value);
            }
        }
        if symbols.is_empty() {
            return Err(format!("{path} has no symbol table"));
        }
        Ok(Elf { segments, symbols })
    }

    /// The value of symbol `name`, if the image has it.
    pub fn symbol(&self, name: &str) -> Option<u32> {
        self.symbols.get(name).copied()
    }

    /// The `len` file bytes at physical address `addr`, if one segment's
    /// data covers all of them (as `TargetImage::ReadAt`).
    pub fn read_at(&self, addr: u32, len: u32) -> Option<&[u8]> {
        self.segments.iter().find_map(|s| {
            let end = u64::from(s.addr) + s.data.len() as u64;
            (addr >= s.addr && u64::from(addr) + u64::from(len) <= end).then(|| {
                let start = (addr - s.addr) as usize;
                &s.data[start..start + len as usize]
            })
        })
    }
}

/// Reads the values of the `wanted` symbols, as qtprogram's flash plan does.
///
/// Lenient where [`Elf`] is strict: a symbol table with a bad entry size or
/// link is skipped rather than refused. Errors are flashplan.cpp's: "not a
/// 32-bit little-endian ELF file" and "truncated ELF section table".
pub fn plan_symbols(data: &[u8], wanted: &[&str]) -> Result<HashMap<String, u32>, String> {
    let mut out = HashMap::new();
    if data.len() < 0x34 || &data[0..4] != b"\x7fELF" || data[4] != 1 || data[5] != 1 {
        return Err("not a 32-bit little-endian ELF file".into());
    }
    let shoff = u32_at(data, 0x20) as u64;
    let shentsize = u16_at(data, 0x2E) as u64;
    let shnum = u16_at(data, 0x30) as u64;
    if shentsize < 40 || shoff + shnum * shentsize > data.len() as u64 {
        return Err("truncated ELF section table".into());
    }
    let section =
        |i: u64, field: u64| u32_at(data, (shoff + i * shentsize + field * 4) as usize) as u64;
    for i in 0..shnum {
        if section(i, 1) != u64::from(SHT_SYMTAB) {
            continue;
        }
        let (offset, size, entsize) = (section(i, 4), section(i, 5), section(i, 9));
        let link = section(i, 6);
        if entsize < 16 || link >= shnum || offset + size > data.len() as u64 {
            continue;
        }
        let strtab = section(link, 4);
        let mut pos = offset;
        while pos + 8 <= offset + size {
            let name = strtab + u64::from(u32_at(data, pos as usize));
            if name < data.len() as u64 {
                let text = &data[name as usize..];
                let end = text.iter().position(|&b| b == 0).unwrap_or(text.len());
                let symbol = String::from_utf8_lossy(&text[..end]);
                if wanted.contains(&symbol.as_ref()) {
                    out.insert(symbol.into_owned(), u32_at(data, pos as usize + 4));
                }
            }
            pos += entsize;
        }
    }
    Ok(out)
}

/// Builds small ARM ELF32 files for tests.
#[cfg(test)]
pub(crate) mod testelf {
    fn put16(b: &mut Vec<u8>, v: u16) {
        b.extend_from_slice(&v.to_le_bytes());
    }
    fn put32(b: &mut Vec<u8>, v: u32) {
        b.extend_from_slice(&v.to_le_bytes());
    }

    /// An ELF with `segments` (physical address, file data, memory size) and
    /// `symbols` (name, value). There is always at least one symbol.
    pub fn build(segments: &[(u32, Vec<u8>, u32)], symbols: &[(&str, u32)]) -> Vec<u8> {
        let phoff = 52u32;
        let mut data_off = phoff + 32 * segments.len() as u32;
        let mut body = Vec::new();
        let mut phdrs = Vec::new();
        for (paddr, bytes, memsz) in segments {
            put32(&mut phdrs, 1); // PT_LOAD
            put32(&mut phdrs, data_off);
            put32(&mut phdrs, *paddr);
            put32(&mut phdrs, *paddr);
            put32(&mut phdrs, bytes.len() as u32);
            put32(&mut phdrs, *memsz);
            put32(&mut phdrs, 5);
            put32(&mut phdrs, 4);
            body.extend_from_slice(bytes);
            data_off += bytes.len() as u32;
        }
        let mut symbols: Vec<(&str, u32)> = symbols.to_vec();
        if symbols.is_empty() {
            symbols.push(("_start", 0));
        }
        let strtab_off = data_off;
        let mut strtab = vec![0u8];
        let mut names = Vec::new();
        for (name, _) in &symbols {
            names.push(strtab.len() as u32);
            strtab.extend_from_slice(name.as_bytes());
            strtab.push(0);
        }
        let symtab_off = strtab_off + strtab.len() as u32;
        let mut symtab = vec![0u8; 16];
        for ((_, value), name) in symbols.iter().zip(&names) {
            put32(&mut symtab, *name);
            put32(&mut symtab, *value);
            put32(&mut symtab, 0);
            symtab.extend_from_slice(&[0, 0]);
            put16(&mut symtab, 1);
        }
        let shoff = symtab_off + symtab.len() as u32;

        let mut f = Vec::new();
        f.extend_from_slice(b"\x7fELF");
        f.extend_from_slice(&[1, 1, 1]);
        f.resize(16, 0);
        put16(&mut f, 2);
        put16(&mut f, 40);
        put32(&mut f, 1);
        put32(&mut f, 0x0800_0001);
        put32(&mut f, phoff);
        put32(&mut f, shoff);
        put32(&mut f, 0);
        put16(&mut f, 52);
        put16(&mut f, 32);
        put16(&mut f, segments.len() as u16);
        put16(&mut f, 40);
        put16(&mut f, 3);
        put16(&mut f, 0);
        f.extend_from_slice(&phdrs);
        f.extend_from_slice(&body);
        f.extend_from_slice(&strtab);
        f.extend_from_slice(&symtab);
        // Section headers: null, .symtab (link 2), .strtab.
        f.extend_from_slice(&[0u8; 40]);
        for v in [0, 2, 0, 0, symtab_off, symtab.len() as u32, 2, 1, 4, 16] {
            put32(&mut f, v);
        }
        for v in [0, 3, 0, 0, strtab_off, strtab.len() as u32, 0, 0, 1, 0] {
            put32(&mut f, v);
        }
        f
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn segments_at_physical_addresses_and_symbols() {
        let elf = testelf::build(
            &[
                (0x0800_0000, vec![1, 2, 3, 4], 4),
                (0x0800_1000, vec![], 0x100),
            ],
            &[("__flash0_base__", 0x0800_0000), ("x", 7)],
        );
        let e = Elf::parse(&elf, "t.elf").unwrap();
        assert_eq!(e.segments.len(), 2);
        assert_eq!(e.segments[0].addr, 0x0800_0000);
        assert_eq!(e.read_at(0x0800_0001, 2), Some(&[2u8, 3][..]));
        assert_eq!(e.read_at(0x0800_0003, 2), None);
        assert_eq!(e.symbol("x"), Some(7));
        assert_eq!(e.symbol("y"), None);
    }

    #[test]
    fn strict_reader_errors_name_the_file() {
        assert_eq!(
            Elf::parse(b"nope", "a.elf").unwrap_err(),
            "a.elf is not an ELF file"
        );
        let mut elf = testelf::build(&[], &[]);
        elf[18] = 3;
        assert_eq!(
            Elf::parse(&elf, "a.elf").unwrap_err(),
            "a.elf is not an ARM image"
        );
    }

    #[test]
    fn plan_symbols_finds_wanted_only() {
        let elf = testelf::build(&[], &[("a", 1), ("b", 2)]);
        let s = plan_symbols(&elf, &["b", "c"]).unwrap();
        assert_eq!(s.len(), 1);
        assert_eq!(s["b"], 2);
        assert_eq!(
            plan_symbols(b"\x7fELF", &[]).unwrap_err(),
            "not a 32-bit little-endian ELF file"
        );
    }
}
