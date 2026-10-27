//! Links the `tagcore_c` library that CMake builds, shared or static.
//!
//! `TAGCORE_C_LIB_DIR` names the directory holding the library; in a CMake
//! build tree that is `<build>/<Config>/lib`. `TAGCORE_C_LINK` chooses how:
//!
//! The default follows the platform's packaging: static on macOS, where each
//! application is its own bundle and would carry its own copy of a shared
//! library anyway; shared on Linux and Windows, where the tools share one
//! install directory and one copy of the library serves them all.
//!
//! - `static` (the macOS default): `libtagcore_c.a`, the
//!   shim pre-linked with the C++ writer, libprotobuf, abseil and SQLite into
//!   one object with only the `tlw_*` functions global (target
//!   `tagcore_c_static`). The executable needs nothing at run time, but links
//!   the C++ runtime and, on macOS, the frameworks libusb uses (it comes in
//!   with tagcore's `Tag`).
//! - `dylib` (the Linux and Windows default):
//!   `libtagcore_c.dylib`, `.so`, or `tagcore_c.lib` with its DLL. The
//!   directory is passed on to dependents' build scripts as
//!   `DEP_TAGCORE_C_LIB_DIR`, since only the final binary's package can set
//!   the run-time search path (rpath) that finds the library.

use std::env;
use std::path::PathBuf;

/// Emits the link directives, or stops the build with how to fix it.
fn main() {
    println!("cargo:rerun-if-env-changed=TAGCORE_C_LIB_DIR");
    println!("cargo:rerun-if-env-changed=TAGCORE_C_LINK");
    let Some(dir) = env::var_os("TAGCORE_C_LIB_DIR").map(PathBuf::from) else {
        panic!(
            "TAGCORE_C_LIB_DIR is not set. Build the C library with CMake \
             (-DBUILD_TAGCORE_C=ON, target tagcore_c or tagcore_c_static) and set \
             TAGCORE_C_LIB_DIR to the directory holding it, e.g. <build>/Release/lib."
        );
    };
    if !dir.is_dir() {
        panic!("TAGCORE_C_LIB_DIR={} is not a directory", dir.display());
    }
    let os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    println!("cargo:rustc-link-search=native={}", dir.display());

    // Static on macOS (one bundle per app), shared elsewhere (one shared
    // install directory for every tool).
    let default_link = if os == "macos" { "static" } else { "dylib" };
    let link = env::var("TAGCORE_C_LINK").unwrap_or_else(|_| default_link.into());
    match link.as_str() {
        "static" => {
            let archive = dir.join("libtagcore_c.a");
            println!("cargo:rerun-if-changed={}", archive.display());
            println!("cargo:rustc-link-lib=static=tagcore_c");
            match os.as_str() {
                "macos" => {
                    println!("cargo:rustc-link-lib=dylib=c++");
                    println!("cargo:rustc-link-lib=dylib=objc");
                    for framework in ["CoreFoundation", "IOKit", "Security"] {
                        println!("cargo:rustc-link-lib=framework={framework}");
                    }
                }
                "linux" => {
                    println!("cargo:rustc-link-lib=dylib=stdc++");
                    println!("cargo:rustc-link-lib=dylib=udev");
                }
                _ => {}
            }
        }
        "dylib" => {
            println!("cargo:rustc-link-lib=dylib=tagcore_c");
            // Read by dependents as DEP_TAGCORE_C_LIB_DIR.
            println!("cargo:lib_dir={}", dir.display());
            // This crate's own tests and examples find the library here.
            if os != "windows" {
                println!("cargo:rustc-link-arg=-Wl,-rpath,{}", dir.display());
            }
        }
        other => panic!("TAGCORE_C_LINK={other}: expected static or dylib"),
    }
}
