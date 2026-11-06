//! The raw C interface, as declared in `rebuild_c.h`.
//!
//! Hand-written: the safe wrapper, `taglogwriter::rebuild_from_capture`,
//! says how to keep it in step with the header. Each item's comment is a
//! summary: the header's Doxygen is the contract.

use core::ffi::{c_char, c_int, c_uint};

/// `RBD_ABI_VERSION` from the header this crate was written against.
pub const RBD_ABI_VERSION: u32 = 1;

unsafe extern "C" {
    /// The interface version the library was built with.
    pub fn rbd_abi_version() -> c_uint;
    /// Rebuilds a SQLite download from a tag-capture directory; 1 when the
    /// file was written, 0 otherwise with `error` saying why.
    pub fn rbd_rebuild(
        capture_dir: *const c_char,
        db_path: *const c_char,
        records: *mut u32,
        error: *mut c_char,
        error_len: usize,
    ) -> c_int;
}
