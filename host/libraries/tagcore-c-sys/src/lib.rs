//! Links `tagcore_c`, the C interface to the C++ host libraries, and declares
//! its functions.
//!
//! The build script links `libtagcore_c` (see `build.rs`), and this is the
//! only crate in a build that declares `links = "tagcore_c"`, as Cargo allows
//! only one. Each part of the C interface is a module of raw declarations,
//! written by hand from its header:
//!
//! | Module | Header | Safe wrapper |
//! | --- | --- | --- |
//! | [`tlw`] | `taglogwriter_c.h` | `taglogwriter` |
//! | [`tcs`] | `calibsession_c.h` | `calibsession` |
//!
//! Programs use the safe wrappers, which may be used together; each keeps
//! its own safety argument and says how to keep its module in step with the
//! header. A build script that needs the library's directory, to set a
//! run-time search path, reads `DEP_TAGCORE_C_LIB_DIR`, which Cargo gives
//! only to direct dependents of this crate.
#![no_std]

pub mod tcs;
pub mod tlw;
