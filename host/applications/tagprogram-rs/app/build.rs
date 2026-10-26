//! Assembles the page Tauri serves and runs Tauri's build step, writing
//! nothing into the source tree.
//!
//! The page lives in `../ui` and loads the shared kit from `host/ui-kit` by a
//! relative path, so it also opens as a plain file in a browser. Tauri serves
//! one directory, so a staging directory under `OUT_DIR` gets `dist/` (the
//! page, with the kit's path rewritten to `ui-kit/`, and the kit), the
//! configuration, the manifest, the capabilities and the icons. tauri-build runs there,
//! because it writes `gen/schemas` relative to its working directory, and
//! `generate_context!` is pointed at the staged `dist/` through
//! `TAURI_CONFIG`. The release build runs with the source tree read-only.

use std::fs;
use std::path::{Path, PathBuf};

const KIT_HREF: &str = "../../../ui-kit/";

/// Copies the files of `from` into `to` (not recursively), rewriting the
/// kit's path in HTML when `rewrite` is set.
fn copy_dir(from: &Path, to: &Path, rewrite: bool) {
    fs::create_dir_all(to).expect("create staging directory");
    for entry in fs::read_dir(from).unwrap_or_else(|e| panic!("read {}: {e}", from.display())) {
        let path = entry.expect("dir entry").path();
        if path.is_dir() {
            continue;
        }
        let dest = to.join(path.file_name().expect("file name"));
        if rewrite && path.extension().is_some_and(|e| e == "html") {
            let html = fs::read_to_string(&path).expect("read html");
            fs::write(&dest, html.replace(KIT_HREF, "ui-kit/")).expect("write html");
        } else {
            fs::copy(&path, &dest).expect("copy file");
        }
    }
}

fn main() {
    let manifest = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").expect("manifest dir"));
    let stage = PathBuf::from(std::env::var("OUT_DIR").expect("OUT_DIR")).join("tauri");
    let _ = fs::remove_dir_all(&stage);

    let dist = stage.join("dist");
    copy_dir(&manifest.join("../ui"), &dist, true);
    copy_dir(&manifest.join("../../../ui-kit"), &dist.join("ui-kit"), false);
    copy_dir(&manifest.join("capabilities"), &stage.join("capabilities"), false);
    copy_dir(&manifest.join("icons"), &stage.join("icons"), false);
    // tauri-build reads both from its working directory: the configuration,
    // and the manifest to check the tauri crate's features against it.
    for file in ["tauri.conf.json", "Cargo.toml"] {
        fs::copy(manifest.join(file), stage.join(file)).expect("copy config");
    }

    for watched in ["../ui", "../../../ui-kit", "capabilities", "icons", "tauri.conf.json", "Cargo.toml"] {
        println!("cargo:rerun-if-changed={}", manifest.join(watched).display());
    }
    // The page generate_context! embeds: the staged copy, by absolute path.
    let config = format!(
        r#"{{"build":{{"frontendDist":{}}}}}"#,
        serde_json::to_string(&dist.to_string_lossy()).expect("path as JSON")
    );
    println!("cargo:rustc-env=TAURI_CONFIG={config}");

    std::env::set_current_dir(&stage).expect("enter staging directory");
    tauri_build::build()
}
