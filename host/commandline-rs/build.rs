//! Sets the run-time search path for `libtagcore_c` on the crate's binaries.
//!
//! With the `sqlitelog` feature, `tag-dwnld`, `tag-log-import` and
//! `tag-rebuild` write through `taglogwriter`, which links `libtagcore_c` through
//! `tagcore-c-sys`. When that link is shared (the Linux and Windows default,
//! or `TAGCORE_C_LINK=dylib`), the binaries need to find the library at run
//! time. Only this package's build script can give its own binaries an
//! rpath, so it is set here, for every binary of the crate, as the
//! applications' `app` build scripts do:
//!
//! - `TAGCORE_C_RPATH` when set: the package build sets `$ORIGIN/../lib`,
//!   where the package installs the library;
//! - else the directory the library was linked from, `DEP_TAGCORE_C_LIB_DIR`
//!   (set by `tagcore-c-sys`, which this package names directly so that it
//!   arrives), which is where the build left it.
//!
//! A static link (the macOS default) needs nothing at run time, and Windows
//! has no rpath: there the DLL must be beside the executable or on `PATH`.
//! Without the feature nothing links the library and no rpath is set.

use std::env;

/// Emits the rpath link argument when the shared library is in use.
fn main() {
    println!("cargo:rerun-if-env-changed=TAGCORE_C_RPATH");
    // DEP_TAGCORE_C_LIB_DIR is present only for a shared link.
    let Ok(lib_dir) = env::var("DEP_TAGCORE_C_LIB_DIR") else {
        return;
    };
    if env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows") {
        return;
    }
    let dir = env::var("TAGCORE_C_RPATH").unwrap_or(lib_dir);
    println!("cargo:rustc-link-arg-bins=-Wl,-rpath,{dir}");
}
