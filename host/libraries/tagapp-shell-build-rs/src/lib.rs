//! The build-script half of `tagapp-shell`: assembles the page Tauri serves
//! and runs Tauri's build step, writing nothing into the source tree.
//!
//! An application's `app/build.rs` is one call, [`build`]. It runs inside
//! that build script, so `CARGO_MANIFEST_DIR`, `OUT_DIR` and the
//! `CARGO_FEATURE_*` variables are the application's.
//!
//! The page lives in the application's `ui/` and loads the shared kit from
//! `host/ui-kit` by a relative path, so it also opens as a plain file in a
//! browser. Tauri serves one directory, so a staging directory under
//! `OUT_DIR` gets the page (with the kit's path rewritten to `ui-kit/`), the
//! kit, the configuration, the manifest, the capabilities and the icons.
//! tauri-build runs there, because it writes `gen/schemas` relative to its
//! working directory, and `generate_context!` is pointed at the staged page
//! through `TAURI_CONFIG`. The release build runs with the source tree
//! read-only.
//!
//! Layout of `$OUT_DIR/tauri`, rebuilt from scratch on every run:
//!
//! ```text
//! dist/            ../ui files, *.html with KIT_HREF rewritten to ui-kit/
//! dist/ui-kit/     host/ui-kit files
//! capabilities/    default.json: this crate's capabilities.json, one for all
//! icons/           ../app/icons
//! tauri.conf.json  Cargo.toml
//! gen/schemas/     written by tauri-build
//! ```
//!
//! Only the top level of each source directory is copied. Any failure
//! panics, which fails the build with the message.

use std::fs;
use std::path::{Path, PathBuf};

/// The kit's path as every page's HTML writes it, relative to its `ui/`
/// (`host/applications/<app>-rs/ui` up to `host/ui-kit/`). Replaced with
/// `ui-kit/` in the staged copy, where the kit sits inside `dist/`.
const KIT_HREF: &str = "../../../ui-kit/";

/// The permissions every application's window gets: the engine's events,
/// file dialogs, the window title, and opening URLs and the installed user
/// guide. One file, so no application drifts from the others.
const CAPABILITIES: &str = include_str!("../capabilities.json");

/// When the binary gets an rpath to a shared `libtagcore_c`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Rpath {
    /// The application links no C++ library (Tag Programmer).
    Never,
    /// Only when this cargo feature of the application is on (Tag Monitor's
    /// `sqlitelog`).
    WithFeature(&'static str),
    /// Always (Tag Calibrator, Tag View, whose cores are in the library).
    Always,
}

/// Copies the files of `from` into `to` (not recursively), rewriting the
/// kit's path in HTML when `rewrite` is set.
///
/// Subdirectories are skipped. Only files ending in `.html` are rewritten;
/// every occurrence of [`KIT_HREF`] in them is replaced.
///
/// # Panics
///
/// Panics if `from` cannot be read or a file cannot be copied or written.
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

/// Emits the rpath to a shared `libtagcore_c` when `rpath` asks for one and
/// the library is linked as a shared library off Windows.
///
/// The binary looks for it at `TAGCORE_C_RPATH` when set (a package build
/// sets `$ORIGIN/../lib`, where the package installs it), else in
/// `TAGCORE_C_LIB_DIR`, where the build left it. Only a direct dependent of
/// `tagcore-c-sys` sees `DEP_TAGCORE_C_LIB_DIR`, so the directory is read from
/// the variable `tagcore-c-sys` reads. macOS links the library statically
/// (unless `TAGCORE_C_LINK=dylib`) and Windows has no rpath, so neither gets
/// one.
fn tagcore_c_rpath(rpath: Rpath) {
    println!("cargo:rerun-if-env-changed=TAGCORE_C_LIB_DIR");
    println!("cargo:rerun-if-env-changed=TAGCORE_C_RPATH");
    println!("cargo:rerun-if-env-changed=TAGCORE_C_LINK");
    let wanted = match rpath {
        Rpath::Never => false,
        Rpath::Always => true,
        Rpath::WithFeature(feature) => {
            let var = format!("CARGO_FEATURE_{}", feature.to_uppercase().replace('-', "_"));
            std::env::var_os(var).is_some()
        }
    };
    let os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    // tagcore-c-sys's own default: static on macOS, shared elsewhere.
    let default_link = if os == "macos" { "static" } else { "dylib" };
    let shared =
        std::env::var("TAGCORE_C_LINK").unwrap_or_else(|_| default_link.into()) == "dylib";
    if wanted && shared && os != "windows" {
        let dir = std::env::var("TAGCORE_C_RPATH").or_else(|_| std::env::var("TAGCORE_C_LIB_DIR"));
        if let Ok(dir) = dir {
            println!("cargo:rustc-link-arg-bins=-Wl,-rpath,{dir}");
        }
    }
}

/// Stages the page, kit, capabilities and Tauri inputs under
/// `$OUT_DIR/tauri`, tells cargo what to watch, sets `TAURI_CONFIG`, adds the
/// `libtagcore_c` rpath that `rpath` asks for, and runs tauri-build from the
/// staging directory. Call it as the whole of an application's `build.rs`.
///
/// # Panics
///
/// Panics if `CARGO_MANIFEST_DIR` or `OUT_DIR` is unset or any staging step
/// fails; tauri-build panics on its own errors.
pub fn build(rpath: Rpath) {
    let manifest = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").expect("manifest dir"));
    let stage = PathBuf::from(std::env::var("OUT_DIR").expect("OUT_DIR")).join("tauri");
    let _ = fs::remove_dir_all(&stage);

    let dist = stage.join("dist");
    copy_dir(&manifest.join("../ui"), &dist, true);
    copy_dir(&manifest.join("../../../ui-kit"), &dist.join("ui-kit"), false);
    fs::create_dir_all(stage.join("capabilities")).expect("create capabilities");
    fs::write(stage.join("capabilities/default.json"), CAPABILITIES).expect("write capabilities");
    copy_dir(&manifest.join("icons"), &stage.join("icons"), false);
    // tauri-build reads both from its working directory: the configuration,
    // and the manifest to check the tauri crate's features against it.
    for file in ["tauri.conf.json", "Cargo.toml"] {
        fs::copy(manifest.join(file), stage.join(file)).expect("copy config");
    }

    for watched in ["../ui", "../../../ui-kit", "icons", "tauri.conf.json", "Cargo.toml"] {
        println!("cargo:rerun-if-changed={}", manifest.join(watched).display());
    }
    // The page generate_context! embeds: the staged copy, by absolute path.
    // Canonical, because Tauri reads frontendDist as a URL when it can: a
    // Windows path such as `d:/a/...` parses as one (scheme `d`), and then
    // nothing is embedded and the window finds no index.html. The canonical
    // form there, `\\?\D:\a\...`, cannot.
    let dist = dist.canonicalize().expect("canonical staging path");
    let config = format!(
        r#"{{"build":{{"frontendDist":{}}}}}"#,
        serde_json::to_string(&dist.to_string_lossy()).expect("path as JSON")
    );
    println!("cargo:rustc-env=TAURI_CONFIG={config}");

    tagcore_c_rpath(rpath);

    std::env::set_current_dir(&stage).expect("enter staging directory");
    tauri_build::build()
}
