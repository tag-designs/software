//! The raw C interface, as declared in `sensorviz_c.h`.
//!
//! Hand-written: the safe wrapper, `sensorviz-session`, says how to keep it in
//! step with the header ("Maintaining"). Each item's comment is a summary:
//! the header's Doxygen is the contract.

use core::ffi::{c_char, c_int};

/// The opaque C handle, `struct svz_session`.
#[repr(C)]
pub struct SvzSession {
    /// Makes the type unconstructible in Rust.
    _private: [u8; 0],
}

/// The opaque C handle, `struct svz_preferences`.
#[repr(C)]
pub struct SvzPreferences {
    /// Makes the type unconstructible in Rust.
    _private: [u8; 0],
}

/// `SVZ_ABI_VERSION` from the header this crate was written against.
pub const SVZ_ABI_VERSION: u32 = 1;
/// `SVZ_GAP_MARKER`: the index of a gap marker in an envelope.
pub const SVZ_GAP_MARKER: u64 = u64::MAX;

unsafe extern "C" {
    /// The interface version the library was built with.
    pub fn svz_abi_version() -> u32;

    /// A new, empty preference store, or null on out-of-memory.
    pub fn svz_preferences_new() -> *mut SvzPreferences;
    /// Frees a store; null is ignored.
    pub fn svz_preferences_free(p: *mut SvzPreferences);
    /// Replaces the store from a file's text; 1 on success.
    pub fn svz_preferences_load(
        p: *mut SvzPreferences,
        json: *const c_char,
        len: usize,
        source: *const c_char,
    ) -> c_int;
    /// The preference file, owned by `p` until its next call.
    pub fn svz_preferences_save(p: *mut SvzPreferences) -> *const c_char;
    /// Why the last load failed, owned by `p`.
    pub fn svz_preferences_last_error(p: *const SvzPreferences) -> *const c_char;

    /// A new session with no log, or null on out-of-memory.
    pub fn svz_session_new() -> *mut SvzSession;
    /// Frees a session; null is ignored.
    pub fn svz_session_free(s: *mut SvzSession);
    /// Why the last failing call failed, owned by `s`.
    pub fn svz_last_error(s: *const SvzSession) -> *const c_char;
    /// Opens a log as File > Load does; 1 on success.
    pub fn svz_open(s: *mut SvzSession, path: *const c_char, p: *const SvzPreferences) -> c_int;
    /// Applies the store's entry for the log's tag; change bits.
    pub fn svz_apply_preferences(s: *mut SvzSession, p: *const SvzPreferences) -> u32;
    /// Stores the log's display choices under its tag.
    pub fn svz_remember_preferences(s: *const SvzSession, p: *mut SvzPreferences);
    /// Removes the store's entry for the log's tag.
    pub fn svz_forget_preferences(s: *const SvzSession, p: *mut SvzPreferences);

    /// The catalog as JSON, owned by `s` until its next call.
    pub fn svz_catalog_json(s: *mut SvzSession) -> *const c_char;
    /// The view state as JSON, owned by `s` until its next call.
    pub fn svz_view_json(s: *mut SvzSession) -> *const c_char;
    /// The PlotSpec as JSON, owned by `s` until its next call.
    pub fn svz_spec_json(s: *mut SvzSession) -> *const c_char;
    /// The applicable parameters as JSON, owned by `s` until its next call.
    pub fn svz_parameters_json(s: *mut SvzSession) -> *const c_char;
    /// The hover rows at a time as a JSON array, owned by `s` until its next call.
    pub fn svz_hover_json(s: *mut SvzSession, time: f64) -> *const c_char;
    /// A time as the readout shows it, owned by `s` until its next call.
    pub fn svz_format_time(s: *mut SvzSession, time: f64) -> *const c_char;
    /// The range dialog's starting range; 1, or 0 for an unknown stream.
    pub fn svz_range_for_dialog(
        s: *const SvzSession,
        id: *const c_char,
        lower: *mut f64,
        upper: *mut f64,
    ) -> c_int;
    /// Copies an M4 envelope; returns its length, writing nothing when the
    /// arrays are too short.
    pub fn svz_envelope(
        s: *mut SvzSession,
        stream: u64,
        t0: f64,
        t1: f64,
        columns: c_int,
        break_gaps: c_int,
        time: *mut f64,
        value: *mut f32,
        index: *mut u64,
        capacity: usize,
    ) -> usize;
    /// Copies the compass samples (seven floats each); returns their number,
    /// writing nothing when the arrays are too short.
    pub fn svz_compass_samples(
        s: *mut SvzSession,
        time: *mut f64,
        values: *mut f32,
        capacity: usize,
    ) -> usize;
    /// Runs the state dump's script on the session; the dump, owned by `s`.
    pub fn svz_state_dump(s: *mut SvzSession) -> *const c_char;

    /// Shows or hides a stream; change bits.
    pub fn svz_set_stream_visible(s: *mut SvzSession, id: *const c_char, visible: c_int) -> u32;
    /// Shows exactly these streams; change bits.
    pub fn svz_set_visible_streams(
        s: *mut SvzSession,
        ids: *const *const c_char,
        count: usize,
    ) -> u32;
    /// Sets a y range and its links; change bits.
    pub fn svz_set_stream_range(
        s: *mut SvzSession,
        id: *const c_char,
        lower: f64,
        upper: f64,
    ) -> u32;
    /// Clears every custom range; change bits.
    pub fn svz_reset_zoom(s: *mut SvzSession) -> u32;
    /// Replaces the color overrides (0xRRGGBB); change bits.
    pub fn svz_set_colors(
        s: *mut SvzSession,
        ids: *const *const c_char,
        rgb: *const u32,
        count: usize,
    ) -> u32;
    /// Replaces the axis-side overrides (nonzero right); change bits.
    pub fn svz_set_axis_sides(
        s: *mut SvzSession,
        ids: *const *const c_char,
        right: *const c_int,
        count: usize,
    ) -> u32;
    /// Sets the title; change bits.
    pub fn svz_set_title(s: *mut SvzSession, title: *const c_char) -> u32;
    /// Shows or hides the title; change bits.
    pub fn svz_set_title_visible(s: *mut SvzSession, visible: c_int) -> u32;
    /// Sets the UTC offset, hours; change bits.
    pub fn svz_set_utc_offset(s: *mut SvzSession, hours: c_int) -> u32;
    /// Shows or hides the cursors; change bits.
    pub fn svz_set_cursors_visible(s: *mut SvzSession, visible: c_int) -> u32;
    /// Breaks lines at gaps or joins them; change bits.
    pub fn svz_set_break_at_gaps(s: *mut SvzSession, break_gaps: c_int) -> u32;
    /// Sets a transform parameter; 1, or 0 when the remake failed.
    pub fn svz_set_parameter(
        s: *mut SvzSession,
        id: *const c_char,
        value: f64,
        changes: *mut u32,
    ) -> c_int;
}
