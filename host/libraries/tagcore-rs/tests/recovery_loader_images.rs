//! Reads every built loader image under `$TAG_LOADER_DIR` with
//! [`TargetImage`] and checks what the recovery code relies on: an SRAM
//! segment, the `Serve` entry and `loaderService` block, and the ST entry
//! points. Skipped when the variable is unset, since the loaders are built
//! by the embedded build, not by cargo.

use std::path::{Path, PathBuf};

use tagcore::recovery::TargetImage;

/// Every `.stldr` under `dir`, recursively.
fn loaders(dir: &Path, out: &mut Vec<PathBuf>) {
    for e in std::fs::read_dir(dir).unwrap().flatten() {
        let p = e.path();
        if p.is_dir() {
            loaders(&p, out);
        } else if p.extension().is_some_and(|x| x == "stldr") {
            out.push(p);
        }
    }
}

/// Each loader parses, has code in SRAM1 and the symbols the host calls.
#[test]
fn built_loaders_parse() {
    let Some(dir) = std::env::var_os("TAG_LOADER_DIR") else {
        eprintln!("TAG_LOADER_DIR unset; skipped");
        return;
    };
    let mut found = Vec::new();
    loaders(Path::new(&dir), &mut found);
    assert!(!found.is_empty(), "no .stldr under {}", dir.to_string_lossy());
    for p in found {
        let img = TargetImage::load(&p).unwrap();
        assert!(
            img.segments().iter().any(|s| (0x2000_0000..0x2003_0000).contains(&s.addr)),
            "{}: no SRAM segment",
            p.display()
        );
        for sym in ["Init", "Read"] {
            assert!(img.symbol(sym).is_some_and(|v| v & 1 == 1), "{}: {sym}", p.display());
        }
        if let Some(serve) = img.symbol("Serve") {
            assert_eq!(serve & 1, 1, "{}: Serve is Thumb", p.display());
            assert!(img.symbol("loaderService").is_some(), "{}: block", p.display());
        }
        // StorageInfo at address 0: the device name, then start and size.
        assert!(img.read_at(104, 8).is_some(), "{}: StorageInfo", p.display());
        eprintln!("{} ok ({} segments)", p.display(), img.segments().len());
    }
}
