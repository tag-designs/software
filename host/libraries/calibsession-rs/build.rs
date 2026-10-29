//! Gives this crate's own tests and examples a run-time search path (rpath)
//! for the shared `libtagcore_c`.
//!
//! `tagcore-c-sys` links the library; when it links the shared one it passes
//! the directory on as `DEP_TAGCORE_C_LIB_DIR`, which only direct dependents
//! receive. A link argument applies only to the binaries of the package whose
//! build script emits it, so each wrapper sets the rpath for its own test and
//! example binaries here; a program's own package does the same for it.
//! Windows has no rpath: the DLL must be beside the executable or on `PATH`.

use std::env;

/// Emits the rpath link argument when the shared library is in use.
fn main() {
    if let Ok(dir) = env::var("DEP_TAGCORE_C_LIB_DIR")
        && env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("windows")
    {
        println!("cargo:rustc-link-arg=-Wl,-rpath,{dir}");
    }
}
