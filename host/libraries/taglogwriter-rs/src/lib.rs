//! Writes tag SQLite logs through the C++ writer.
//!
//! [`LogWriter`] is a safe wrapper over the C interface in
//! `host/libraries/tagcore-c/taglogwriter_c.h`, which wraps the C++
//! `SqliteTagLogWriter` that `tag-dwnld`, `tag-log-import` and the Qt
//! applications use. Each message crosses as its protobuf wire encoding, so
//! the database is exactly the one the C++ tools write: one writer, one
//! schema, and no log format interpreted on the Rust side.
//!
//! The sequence follows a download:
//!
//! ```no_run
//! use taglogwriter::{LogHeader, LogWriter};
//! # fn read_page(index: usize) -> tagcore::proto::Ack { unimplemented!() }
//! # let header: LogHeader = unimplemented!();
//! let mut writer = LogWriter::create("tag.db3".as_ref(), &header.config, true)?;
//! writer.write_header(&header)?;
//! writer.begin_log()?;
//! let mut index = 0;
//! loop {
//!     // The writer says how far each page advances the download, and
//!     // where it ends: both depend on the tag type.
//!     let consumed = writer.write_log(&read_page(index))?;
//!     if consumed == 0 {
//!         break;
//!     }
//!     index += consumed;
//! }
//! writer.end_log()?;
//! # Ok::<(), taglogwriter::WriterError>(())
//! ```
//!
//! # Linking
//!
//! The crate links `libtagcore_c`, which CMake builds with
//! `-DBUILD_TAGCORE_C=ON`; `build.rs` finds it through `TAGCORE_C_LIB_DIR`.
//! On macOS it links the static archive (`tagcore_c_static`), since each
//! application is its own bundle, and the executable needs nothing at run
//! time. On Linux and Windows it links the shared library, one copy of which
//! serves every tool in the install directory; a binary must find it at run
//! time: on macOS and Linux its package's build script sets
//! an rpath from `DEP_TAGCORE_C_LIB_DIR`; on Windows the DLL must sit beside
//! the executable or on `PATH`.

use std::ffi::{CStr, CString, c_int};
use std::fmt;
use std::path::Path;
use std::ptr::NonNull;

use prost::Message;
use tagcore::proto::{Ack, CalibrationConstants, Config, State, TagInfo};

/// The raw C interface, as declared in `taglogwriter_c.h`.
mod ffi {
    use std::ffi::{c_char, c_int};

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
}

/// Why a writer call failed: the C++ writer's own message, or the
/// interface's (a message that did not parse, a version mismatch).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct WriterError(pub String);

impl fmt::Display for WriterError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for WriterError {}

/// Sets how much the C++ writer logs to stderr: everything at DEBUG when
/// `enabled`, errors only otherwise, as the C++ tools' `-d` does. It logs at
/// DEBUG until this is called. Process-wide.
pub fn set_debug(enabled: bool) {
    // SAFETY: no preconditions; it sets one integer in the library.
    unsafe { ffi::tlw_set_debug(c_int::from(enabled)) }
}

/// Everything a log records about the tag before its data: the C++
/// `TagLogHeader`.
///
/// A live download fills it with the same calls, in the same order, as the
/// C++ `readTagLogHeader()`: Config, TagInfo, every stored set of
/// calibration constants from index 0, then the state log in chunks.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct LogHeader {
    /// The tag's configuration. Its tag type chooses the schema.
    pub config: Config,
    /// The tag's identity; its UUID is written to the info table.
    pub info: TagInfo,
    /// Stored calibration constants, oldest first.
    pub calibration: Vec<CalibrationConstants>,
    /// State history, in the order the tag returned it.
    pub states: Vec<State>,
}

/// One SQLite log being written by the C++ writer.
///
/// Create it, write the header, then the pages between
/// [`begin_log`](Self::begin_log) and [`end_log`](Self::end_log). Dropping
/// it closes the database; pages written since `begin_log` without an
/// `end_log` are rolled back.
pub struct LogWriter {
    /// The C handle; never null, and owned: closed exactly once, in `Drop`.
    raw: NonNull<ffi::TlwWriter>,
}

// SAFETY: the C handle carries no thread affinity (no thread-local state, no
// thread-bound SQLite connection mode); it requires only that one thread use
// it at a time, which `&mut self` on every writing call guarantees. It is
// not `Sync`.
unsafe impl Send for LogWriter {}

impl LogWriter {
    /// Creates a log at `path` for a tag with `config`, replacing an
    /// existing file when `replace` is true.
    ///
    /// # Errors
    ///
    /// When the library was built from a different interface version, the
    /// path is not UTF-8 or contains a NUL, or the database cannot be
    /// created (the writer's message says why).
    pub fn create(path: &Path, config: &Config, replace: bool) -> Result<Self, WriterError> {
        // SAFETY: no arguments, no preconditions.
        let version = unsafe { ffi::tlw_abi_version() };
        if version != ffi::TLW_ABI_VERSION {
            return Err(WriterError(format!(
                "libtagcore_c has interface version {version}; this program needs {}",
                ffi::TLW_ABI_VERSION
            )));
        }
        let path = path
            .to_str()
            .ok_or_else(|| WriterError(format!("Path is not UTF-8: {}", path.display())))?;
        let path = CString::new(path)
            .map_err(|_| WriterError(format!("Path contains a NUL byte: {path}")))?;
        let config = config.encode_to_vec();
        // SAFETY: `path` is a NUL-terminated string and `config` a buffer of
        // `config.len()` bytes, both live for the call; the library copies
        // what it keeps.
        let raw = unsafe {
            ffi::tlw_open(
                path.as_ptr(),
                config.as_ptr(),
                config.len(),
                c_int::from(replace),
            )
        };
        let raw = NonNull::new(raw).ok_or_else(|| WriterError("Out of memory".into()))?;
        let writer = Self { raw };
        // SAFETY: `raw` is a live handle from tlw_open().
        if unsafe { ffi::tlw_is_open(writer.raw.as_ptr()) } == 0 {
            return Err(writer.last_error());
        }
        Ok(writer)
    }

    /// Writes `header`: its parts, then the info, calibration and state
    /// tables and the tag's data tables. Call once, first.
    ///
    /// # Errors
    ///
    /// When a part is rejected or the writer fails, for example because the
    /// header's tag type differs from the one given to
    /// [`create`](Self::create).
    pub fn write_header(&mut self, header: &LogHeader) -> Result<(), WriterError> {
        self.add_part(ffi::TLW_CONFIG, &header.config)?;
        self.add_part(ffi::TLW_INFO, &header.info)?;
        for constants in &header.calibration {
            self.add_part(ffi::TLW_CALIBRATION, constants)?;
        }
        for state in &header.states {
            self.add_part(ffi::TLW_STATE, state)?;
        }
        // SAFETY: `raw` is a live handle, used from this thread only.
        self.check(unsafe { ffi::tlw_write_header(self.raw.as_ptr()) })
    }

    /// Adds a row to the info table, after the header.
    ///
    /// # Errors
    ///
    /// When a string contains a NUL byte, or the writer fails.
    pub fn write_info(&mut self, field: &str, value: &str) -> Result<(), WriterError> {
        let field = CString::new(field)
            .map_err(|_| WriterError(format!("Info field contains a NUL byte: {field}")))?;
        let value = CString::new(value)
            .map_err(|_| WriterError("Info value contains a NUL byte".into()))?;
        // SAFETY: both strings are NUL-terminated and live for the call.
        self.check(unsafe {
            ffi::tlw_write_info(self.raw.as_ptr(), field.as_ptr(), value.as_ptr())
        })
    }

    /// Starts the data log: the pages that follow are written in one
    /// transaction, committed by [`end_log`](Self::end_log).
    ///
    /// # Errors
    ///
    /// When the transaction cannot be started.
    pub fn begin_log(&mut self) -> Result<(), WriterError> {
        // SAFETY: `raw` is a live handle, used from this thread only.
        self.check(unsafe { ffi::tlw_begin_log(self.raw.as_ptr()) })
    }

    /// Decodes and writes one data-log page, the Ack as the tag returned it.
    ///
    /// Returns how many log records the page consumed, which advances the
    /// index of the next page to request, or 0 at the end of the log. Both
    /// depend on the tag type, which is why the writer decides them.
    ///
    /// # Errors
    ///
    /// When the writer rejects the page or the database write fails.
    pub fn write_log(&mut self, ack: &Ack) -> Result<usize, WriterError> {
        let bytes = ack.encode_to_vec();
        // SAFETY: `bytes` is a buffer of `bytes.len()` bytes, live for the
        // call; `raw` is a live handle, used from this thread only.
        let consumed =
            unsafe { ffi::tlw_write_log(self.raw.as_ptr(), bytes.as_ptr(), bytes.len()) };
        usize::try_from(consumed).map_err(|_| self.last_error())
    }

    /// Commits the data log.
    ///
    /// # Errors
    ///
    /// When the commit fails.
    pub fn end_log(&mut self) -> Result<(), WriterError> {
        // SAFETY: `raw` is a live handle, used from this thread only.
        self.check(unsafe { ffi::tlw_end_log(self.raw.as_ptr()) })
    }

    /// Passes one header part to the writer.
    ///
    /// # Errors
    ///
    /// When the library cannot parse the encoding (a version mismatch).
    fn add_part(&mut self, kind: c_int, message: &impl Message) -> Result<(), WriterError> {
        let bytes = message.encode_to_vec();
        // SAFETY: `bytes` is a buffer of `bytes.len()` bytes, live for the
        // call; `raw` is a live handle, used from this thread only.
        self.check(unsafe {
            ffi::tlw_header_add(self.raw.as_ptr(), kind, bytes.as_ptr(), bytes.len())
        })
    }

    /// Turns a C status (1 success, 0 failure) into a `Result`.
    ///
    /// # Errors
    ///
    /// When `status` is not 1, with the handle's last error.
    fn check(&self, status: c_int) -> Result<(), WriterError> {
        if status == 1 {
            Ok(())
        } else {
            Err(self.last_error())
        }
    }

    /// The handle's last error, copied out of the library.
    fn last_error(&self) -> WriterError {
        // SAFETY: `raw` is a live handle; the returned string is
        // NUL-terminated and valid until the next call on the handle, and is
        // copied before then.
        let text = unsafe { CStr::from_ptr(ffi::tlw_last_error(self.raw.as_ptr())) };
        let text = text.to_string_lossy();
        WriterError(if text.is_empty() {
            "The log writer failed without saying why".into()
        } else {
            text.into_owned()
        })
    }
}

impl Drop for LogWriter {
    fn drop(&mut self) {
        // SAFETY: `raw` came from tlw_open() and is closed only here.
        unsafe { ffi::tlw_close(self.raw.as_ptr()) }
    }
}
