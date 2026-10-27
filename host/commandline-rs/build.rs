//! Sets the run-time search path for `libtagcore_c` on `tag-dwnld`.
//!
//! With the `sqlitelog` feature, the `taglogwriter` crate links the shared
//! library and passes its directory on as `DEP_TAGCORE_C_LIB_DIR`; only this
//! package's build script can give its own binary an rpath. Windows has no
//! rpath: the DLL must be beside the executable or on `PATH`.

use std::env;

/// Emits the rpath link argument when the library is in use.
fn main() {
    if let Ok(dir) = env::var("DEP_TAGCORE_C_LIB_DIR")
        && env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("windows")
    {
        println!("cargo:rustc-link-arg-bin=tag-dwnld=-Wl,-rpath,{dir}");
    }
}
