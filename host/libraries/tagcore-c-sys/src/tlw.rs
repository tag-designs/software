//! The raw C interface, as declared in `taglogwriter_c.h`.
//!
//! Hand-written: the safe wrapper, `taglogwriter`, says how to keep it in
//! step with the header ("Maintaining"). Each item's comment is a summary:
//! the header's Doxygen is the contract.

use core::ffi::{c_char, c_int};

/// The opaque C handle, `struct tlw_writer`.
#[repr(C)]
pub struct TlwWriter {
    /// Makes the type unconstructible and unsized-like in Rust.
    _private: [u8; 0],
}

/// `TLW_ABI_VERSION` from the header this crate was written against.
pub const TLW_ABI_VERSION: u32 = 1;
/// `TLW_CONFIG`: a Config header part.
pub const TLW_CONFIG: c_int = 1;
/// `TLW_INFO`: the TagInfo header part.
pub const TLW_INFO: c_int = 2;
/// `TLW_CALIBRATION`: one CalibrationConstants header part.
pub const TLW_CALIBRATION: c_int = 3;
/// `TLW_STATE`: one State header part.
pub const TLW_STATE: c_int = 4;

unsafe extern "C" {
    /// The interface version the library was built with.
    pub fn tlw_abi_version() -> u32;
    /// Logs at DEBUG when nonzero, errors only otherwise.
    pub fn tlw_set_debug(enabled: c_int);
    /// Creates a log; returns a handle (closed on failure) or null on
    /// out-of-memory.
    pub fn tlw_open(
        path: *const c_char,
        config: *const u8,
        config_len: usize,
        replace_existing: c_int,
    ) -> *mut TlwWriter;
    /// 1 if the database is open.
    pub fn tlw_is_open(w: *const TlwWriter) -> c_int;
    /// Adds one header part; 1 on success.
    pub fn tlw_header_add(w: *mut TlwWriter, kind: c_int, msg: *const u8, len: usize) -> c_int;
    /// Writes the gathered header; 1 on success.
    pub fn tlw_write_header(w: *mut TlwWriter) -> c_int;
    /// Adds an info row; 1 on success.
    pub fn tlw_write_info(
        w: *mut TlwWriter,
        field: *const c_char,
        value: *const c_char,
    ) -> c_int;
    /// Starts the log transaction; 1 on success.
    pub fn tlw_begin_log(w: *mut TlwWriter) -> c_int;
    /// Writes one page; records consumed, 0 at end, negative on error.
    pub fn tlw_write_log(w: *mut TlwWriter, ack: *const u8, len: usize) -> c_int;
    /// Commits the log transaction; 1 on success.
    pub fn tlw_end_log(w: *mut TlwWriter) -> c_int;
    /// The last error on `w`, valid until the next call on it.
    pub fn tlw_last_error(w: *const TlwWriter) -> *const c_char;
    /// Closes and frees `w`.
    pub fn tlw_close(w: *mut TlwWriter);
}
