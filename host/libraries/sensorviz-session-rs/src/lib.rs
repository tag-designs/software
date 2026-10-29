//! Tag logs opened and viewed through sensorViz's C++ core.
//!
//! [`Session`] is a safe wrapper over the C interface in
//! `host/libraries/tagcore-c/sensorviz_c.h`, which wraps the Qt-free
//! `sensorviz::Session` -- the code the Qt sensorViz runs. So a Rust program
//! shows a log exactly as sensorViz does: the same log reader, display
//! transforms (altitude, IMU magnitudes, compass), plot policy, hover text and
//! preference file.
//!
//! ```no_run
//! use sensorviz_session::{Preferences, Session};
//! let preferences = Preferences::new()?;
//! let mut session = Session::new()?;
//! session.open("log.db3", Some(&preferences))?;
//! let spec = session.spec()?;
//! for trace in &spec.traces {
//!     let envelope = session.envelope(trace.stream, f64::NEG_INFINITY, f64::INFINITY, 1200,
//!                                     spec.break_at_gaps);
//!     // draw envelope.time / envelope.value
//! }
//! let changes = session.set_stream_range("imu_pressure", 950.0, 1010.0);
//! if changes.contains(sensorviz_session::Changes::RANGES) {
//!     // redraw
//! }
//! # Ok::<(), sensorviz_session::Error>(())
//! ```
//!
//! Descriptions -- the catalog, the view state, the plot spec, the
//! parameters -- cross as JSON and are parsed into the types here; samples
//! cross as arrays. Times are seconds in the log's time domain: since the Unix
//! epoch, or since the start of collection for IMUTag.
//!
//! # Linking
//!
//! As for `taglogwriter` and `calibsession`: `tagcore-c-sys` links
//! `libtagcore_c`, found through `TAGCORE_C_LIB_DIR`, statically on macOS and
//! shared on Linux and Windows, and a program may use this crate beside them.
//!
//! # Safety model
//!
//! All `unsafe` is in this file. A [`Session`] or [`Preferences`] owns exactly
//! one C handle, never null, freed exactly once in `Drop`. Every pointer
//! passed in points at Rust memory that lives until the call returns, sized
//! as the header requires. A string the library returns belongs to the handle
//! and stays valid until the next call on it, so it is copied before any
//! other call. Each handle may move between threads but not be shared: both
//! types are `Send` and not `Sync`, and every call that changes or reads
//! through the handle's string buffer takes `&mut self`.
//!
//! # Maintaining
//!
//! The declarations are `tagcore_c_sys::svz`
//! (`host/libraries/tagcore-c-sys/src/svz.rs`), written by hand from
//! `sensorviz_c.h`. When the header changes, change the declaration and the
//! safe method over it. If a signature, meaning or JSON schema changed, the
//! header raises `SVZ_ABI_VERSION`, and `svz::SVZ_ABI_VERSION` must follow;
//! [`Session::new`] refuses a library built from another version. The JSON
//! types here mirror the writers in `sensorviz_c.cc`; a field added there is
//! ignored here until it is added.

use std::collections::BTreeMap;
use std::ffi::{CStr, CString, c_char, c_int};
use std::fmt;
use std::path::Path;
use std::ptr::NonNull;

use serde::Deserialize;
use tagcore_c_sys::svz as ffi;

/// Why a call failed.
#[derive(Debug, Clone, PartialEq)]
pub enum Error {
    /// The library implements another version of the interface.
    AbiMismatch {
        /// The version this crate was written against.
        expected: u32,
        /// The version the library reports.
        found: u32,
    },
    /// The library could not allocate a handle.
    OutOfMemory,
    /// A string argument held a NUL byte, which C cannot take.
    Nul,
    /// The library's own message: a log that cannot be read, a preference
    /// file refused, a transform that failed to remake its outputs.
    Library(String),
    /// The library returned JSON this crate could not parse: a version skew
    /// the ABI version did not catch.
    Json(String),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::AbiMismatch { expected, found } => write!(
                f,
                "tagcore_c implements sensorviz interface version {found}, this crate needs {expected}"
            ),
            Error::OutOfMemory => write!(f, "out of memory creating a sensorviz handle"),
            Error::Nul => write!(f, "a string argument contains a NUL byte"),
            Error::Library(message) => write!(f, "{message}"),
            Error::Json(message) => write!(f, "unexpected JSON from tagcore_c: {message}"),
        }
    }
}

impl std::error::Error for Error {}

/// What a call changed, as the `SVZ_CHANGE_*` bits: what a front end redraws.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Changes(pub u32);

impl Changes {
    /// Streams were added, replaced or removed.
    pub const STREAMS: Changes = Changes(0x001);
    /// Which streams are shown changed.
    pub const VISIBILITY: Changes = Changes(0x002);
    /// Custom y ranges changed.
    pub const RANGES: Changes = Changes(0x004);
    /// Colors or axis sides changed.
    pub const APPEARANCE: Changes = Changes(0x008);
    /// Title text or visibility changed.
    pub const TITLE: Changes = Changes(0x010);
    /// The UTC offset changed.
    pub const UTC_OFFSET: Changes = Changes(0x020);
    /// Cursor visibility changed.
    pub const CURSORS: Changes = Changes(0x040);
    /// The gap mode changed.
    pub const GAPS: Changes = Changes(0x080);
    /// A transform parameter changed.
    pub const PARAMETERS: Changes = Changes(0x100);

    /// True when nothing changed.
    pub fn is_empty(self) -> bool {
        self.0 == 0
    }
    /// True when every bit of `other` is set here.
    pub fn contains(self, other: Changes) -> bool {
        self.0 & other.0 == other.0
    }
}

impl std::ops::BitOr for Changes {
    type Output = Changes;
    fn bitor(self, other: Changes) -> Changes {
        Changes(self.0 | other.0)
    }
}

/// A time domain.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum TimeDomain {
    /// Seconds since the Unix epoch.
    Epoch,
    /// Seconds since the start of collection (IMUTag).
    Elapsed,
}

/// A side of the plot.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Side {
    /// Left of the data area.
    Left,
    /// Right of the data area.
    Right,
}

/// A multi-column table that transforms read, such as `compass_raw`.
#[derive(Debug, Clone, PartialEq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RecordSet {
    /// Stable id.
    pub id: String,
    /// User-facing name.
    pub label: String,
    /// SQLite table.
    pub table: String,
    /// Domain of its times.
    pub time_domain: TimeDomain,
    /// Column names.
    pub columns: Vec<String>,
}

/// Discrete events drawn as vertical lines.
#[derive(Debug, Clone, PartialEq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct EventMarker {
    /// Stable id, e.g. `imu_resync`.
    pub id: String,
    /// User-facing name.
    pub label: String,
    /// Domain of `times`.
    pub time_domain: TimeDomain,
    /// Event times, seconds.
    pub times: Vec<f64>,
}

/// The newest compass calibration in the log.
#[derive(Debug, Clone, PartialEq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CompassCalibration {
    /// Whether a usable calibration was read.
    pub present: bool,
    /// Its Calibration row's epoch, seconds.
    pub epoch: i64,
    /// Why it is absent or suspect; empty when there is nothing to say.
    pub warning: String,
    /// Hard-iron offset, µT.
    pub hard_iron: [f64; 3],
    /// Soft-iron matrix, row-major.
    pub soft_iron: [f64; 9],
    /// Accelerometer zero-g offset, mg.
    pub accel_offset: [f64; 3],
}

/// A transform that failed when the log was opened.
#[derive(Debug, Clone, PartialEq, Deserialize)]
pub struct TransformFailure {
    /// The transform's id, e.g. `compass`.
    pub transform: String,
    /// Why.
    pub error: String,
}

/// The open log: what it is and what it holds besides its streams.
#[derive(Debug, Clone, PartialEq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Catalog {
    /// The file, as opened; empty when none is.
    pub path: String,
    /// The `info` table's tag type; may be empty.
    pub tag_type: String,
    /// A display name for the tag family.
    pub profile_name: String,
    /// The key its preferences are stored under.
    pub preference_key: String,
    /// The `info` table.
    pub info: BTreeMap<String, String>,
    /// The plot's time domain.
    pub time_domain: TimeDomain,
    /// The absolute start of collection, ms since the Unix epoch, when known.
    pub collection_start_epoch_ms: Option<i64>,
    /// Record sets.
    pub record_sets: Vec<RecordSet>,
    /// Event markers.
    pub event_markers: Vec<EventMarker>,
    /// The compass calibration.
    pub compass_calibration: CompassCalibration,
    /// Where the compass samples came from, for File Info; empty without a compass.
    pub compass_source: String,
    /// Transforms that failed when the log was opened.
    pub transform_failures: Vec<TransformFailure>,
}

/// One stream: what it is, how it is drawn, and its plot data.
#[derive(Debug, Clone, PartialEq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Stream {
    /// Stable id, e.g. `imu_ax`.
    pub id: String,
    /// User-facing name.
    pub label: String,
    /// Units; may be empty.
    pub units: String,
    /// Axis group; empty for a stream with its own axis.
    pub group: String,
    /// The group's user-facing name.
    pub group_label: String,
    /// Default color, `#rrggbb`.
    pub color: String,
    /// The color it is drawn in: the user's, else the default.
    pub effective_color: String,
    /// Derived streams have no visibility of their own.
    pub derived: bool,
    /// Visible on load.
    pub default_visible: bool,
    /// Default axis side.
    pub axis_side: Side,
    /// The side it is drawn on: the user's, else the default.
    pub effective_axis_side: Side,
    /// Default fixed y range.
    pub axis_range: Option<[f64; 2]>,
    /// Domain of its times.
    pub time_domain: TimeDomain,
    /// Its plot-data handle, for [`Session::envelope`].
    pub stream: u64,
    /// Whether it is shown.
    pub visible: bool,
}

/// A y range the user set, or one linked to such a range.
#[derive(Debug, Clone, Copy, PartialEq, Deserialize)]
pub struct CustomRange {
    /// Lower bound.
    pub lower: f64,
    /// Upper bound.
    pub upper: f64,
    /// Set directly, rather than through a linked stream.
    pub explicit: bool,
}

/// The view state: the streams and every choice about them.
#[derive(Debug, Clone, PartialEq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct View {
    /// Every stream, in View-menu order.
    pub streams: Vec<Stream>,
    /// The shown streams, in drawing order.
    pub visible_stream_ids: Vec<String>,
    /// Custom y ranges by stream id.
    pub custom_ranges: BTreeMap<String, CustomRange>,
    /// Graph title.
    pub title: String,
    /// Whether the title is shown.
    pub title_visible: bool,
    /// Hours added to UTC for epoch times.
    pub utc_offset_hours: i32,
    /// Whether the time cursors are shown.
    pub cursors_visible: bool,
    /// Whether lines break at gaps.
    pub break_at_gaps: bool,
    /// Every transform parameter by id; toggles as 1 or 0.
    pub parameters: BTreeMap<String, f64>,
}

/// One drawn trace.
#[derive(Debug, Clone, PartialEq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Trace {
    /// Stream id.
    pub id: String,
    /// Legend name.
    pub name: String,
    /// Plot-data handle, for [`Session::envelope`].
    pub stream: u64,
    /// Line color, `#rrggbb`.
    pub color: String,
    /// Line width, pixels.
    pub pen_width: i32,
    /// Index into [`Spec::axes`].
    pub axis: usize,
}

/// How an axis's range is chosen.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum AxisMode {
    /// From the data, padded, and widened to include the range it showed
    /// before (the renderer keeps that history).
    Auto,
    /// The padded union of an axis group's data ranges.
    Linked,
    /// Exactly `range`.
    Fixed,
}

/// One y axis.
#[derive(Debug, Clone, PartialEq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Axis {
    /// Which side.
    pub side: Side,
    /// Title.
    pub label: String,
    /// Axis, tick and label color, `#rrggbb`.
    pub color: String,
    /// How the range is chosen.
    pub mode: AxisMode,
    /// The axis group when linked.
    pub linked_group: String,
    /// Indices into [`Spec::traces`].
    pub traces: Vec<usize>,
    /// The range per `mode`; none for an auto axis without finite data.
    pub range: Option<[f64; 2]>,
}

/// What the plot shows: everything a renderer needs besides the samples.
#[derive(Debug, Clone, PartialEq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Spec {
    /// The time axis.
    pub time_domain: TimeDomain,
    /// Hours added to UTC for epoch tick labels.
    pub utc_offset_hours: i32,
    /// Earliest and latest sample over all traces; none when nothing is drawn.
    pub x_data_range: Option<[f64; 2]>,
    /// Traces in drawing order.
    pub traces: Vec<Trace>,
    /// Axes, the first outermost.
    pub axes: Vec<Axis>,
    /// Vertical marker lines.
    pub event_marker_times: Vec<f64>,
    /// Lines of the metadata box; empty hides it.
    pub metadata_rows: Vec<String>,
    /// Title; empty hides it.
    pub title: String,
    /// Lines break at gaps.
    pub break_at_gaps: bool,
}

/// A transform parameter, described for building its control.
#[derive(Debug, Clone, PartialEq, Deserialize)]
pub struct Parameter {
    /// Id, for [`Session::set_parameter`].
    pub id: String,
    /// Dialog title.
    pub title: String,
    /// Field label.
    pub prompt: String,
    /// Units; may be empty.
    pub units: String,
    /// `number` or `toggle`.
    pub kind: String,
    /// Smallest value (number).
    pub minimum: f64,
    /// Largest value (number).
    pub maximum: f64,
    /// Decimals shown (number).
    pub decimals: i32,
    /// The current value; a toggle as 1 or 0.
    pub value: f64,
}

/// One stream's M4 envelope: at most four points per column, plus gap markers.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct Envelope {
    /// Point times, seconds.
    pub time: Vec<f64>,
    /// Point values as stored; NaN at a gap marker.
    pub value: Vec<f32>,
    /// Sample index of each point; `None` at a gap marker.
    pub index: Vec<Option<u64>>,
}

/// One compass orientation sample.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct OrientationSample {
    /// Time, seconds.
    pub time: f64,
    /// Magnetic-frame orientation, `[w, x, y, z]`.
    pub q: [f32; 4],
    /// Magnetic dip, degrees.
    pub dip: f32,
    /// Field magnitude, µT.
    pub field: f32,
    /// Acceleration magnitude, mg.
    pub mg: f32,
}

/// Checks the library's interface version.
fn check_abi() -> Result<(), Error> {
    // SAFETY: takes nothing and returns a number.
    let found = unsafe { ffi::svz_abi_version() };
    if found == ffi::SVZ_ABI_VERSION {
        Ok(())
    } else {
        Err(Error::AbiMismatch {
            expected: ffi::SVZ_ABI_VERSION,
            found,
        })
    }
}

/// Copies a library string; null reads as empty.
///
/// # Safety
///
/// `text` is null or a NUL-terminated string that stays valid for the call.
unsafe fn copy(text: *const c_char) -> String {
    if text.is_null() {
        return String::new();
    }
    // SAFETY: non-null and NUL-terminated, as the caller promises.
    unsafe { CStr::from_ptr(text) }
        .to_string_lossy()
        .into_owned()
}

/// `text` as a C string.
fn c_string(text: &str) -> Result<CString, Error> {
    CString::new(text).map_err(|_| Error::Nul)
}

/// Strings as C strings, and the pointer array the library takes.
fn c_strings(items: &[&str]) -> Result<(Vec<CString>, Vec<*const c_char>), Error> {
    let owned = items
        .iter()
        .map(|item| c_string(item))
        .collect::<Result<Vec<_>, _>>()?;
    let pointers = owned.iter().map(|item| item.as_ptr()).collect();
    Ok((owned, pointers))
}

/// Parses JSON from the library.
fn parse<T: for<'a> Deserialize<'a>>(json: &str) -> Result<T, Error> {
    serde_json::from_str(json).map_err(|e| Error::Json(e.to_string()))
}

/// The per-tag display preferences: colors, axis sides, visibility and gaps
/// that follow a tag type, and their JSON file. Outlives sessions.
pub struct Preferences {
    /// The C handle; never null.
    handle: NonNull<ffi::SvzPreferences>,
}

// SAFETY: the handle is used from one thread at a time, which `&mut self`
// on every call that touches its buffers ensures; it may move between threads.
unsafe impl Send for Preferences {}

impl Preferences {
    /// An empty store.
    ///
    /// # Errors
    ///
    /// [`Error::AbiMismatch`] for a library of another interface version,
    /// [`Error::OutOfMemory`] when it cannot allocate.
    pub fn new() -> Result<Self, Error> {
        check_abi()?;
        // SAFETY: takes nothing; returns a new handle or null.
        let handle = unsafe { ffi::svz_preferences_new() };
        NonNull::new(handle)
            .map(|handle| Preferences { handle })
            .ok_or(Error::OutOfMemory)
    }

    /// Replaces every entry with those of a preference file.
    ///
    /// # Errors
    ///
    /// [`Error::Library`] with the reason when the file is refused, leaving
    /// the store as it was; [`Error::Nul`] when `source` holds a NUL.
    pub fn load(&mut self, json: &str, source: &str) -> Result<(), Error> {
        let source = c_string(source)?;
        // SAFETY: a live handle; `json` is `json.len()` bytes and `source`
        // NUL-terminated, both live for the call; the library copies them.
        let ok = unsafe {
            ffi::svz_preferences_load(
                self.handle.as_ptr(),
                json.as_ptr().cast(),
                json.len(),
                source.as_ptr(),
            )
        };
        if ok == 1 {
            Ok(())
        } else {
            // SAFETY: a live handle; the message is copied before the next call.
            Err(Error::Library(unsafe {
                copy(ffi::svz_preferences_last_error(self.handle.as_ptr()))
            }))
        }
    }

    /// The preference file, byte for byte as the Qt sensorViz writes it.
    pub fn save(&mut self) -> String {
        // SAFETY: a live handle; the text is copied before the next call.
        unsafe { copy(ffi::svz_preferences_save(self.handle.as_ptr())) }
    }
}

impl Drop for Preferences {
    fn drop(&mut self) {
        // SAFETY: the handle is live and freed only here.
        unsafe { ffi::svz_preferences_free(self.handle.as_ptr()) }
    }
}

/// One opened log: its catalog, the view state, the transforms, and what the
/// plot and readout show.
pub struct Session {
    /// The C handle; never null.
    handle: NonNull<ffi::SvzSession>,
}

// SAFETY: as for Preferences.
unsafe impl Send for Session {}

impl Session {
    /// A session with no log.
    ///
    /// # Errors
    ///
    /// [`Error::AbiMismatch`] for a library of another interface version,
    /// [`Error::OutOfMemory`] when it cannot allocate.
    pub fn new() -> Result<Self, Error> {
        check_abi()?;
        // SAFETY: takes nothing; returns a new handle or null.
        let handle = unsafe { ffi::svz_session_new() };
        NonNull::new(handle)
            .map(|handle| Session { handle })
            .ok_or(Error::OutOfMemory)
    }

    /// The handle, for a call.
    fn raw(&self) -> *mut ffi::SvzSession {
        self.handle.as_ptr()
    }

    /// The library's last error on this session.
    fn last_error(&self) -> Error {
        // SAFETY: a live handle; the message is copied at once.
        Error::Library(unsafe { copy(ffi::svz_last_error(self.raw())) })
    }

    /// Copies a string the library returned for this session.
    fn returned(&mut self, text: *const c_char) -> String {
        // SAFETY: the string belongs to this live handle and is valid until
        // its next call, which cannot happen while `&mut self` is held here.
        unsafe { copy(text) }
    }

    /// Opens a log and does everything sensorViz's File > Load does: reads
    /// it, resets the view, runs the transforms and applies `preferences`'
    /// entry for its tag.
    ///
    /// # Errors
    ///
    /// [`Error::Library`] when the log cannot be read, leaving the session as
    /// it was; [`Error::Nul`] when the path holds a NUL.
    pub fn open(
        &mut self,
        path: impl AsRef<Path>,
        preferences: Option<&Preferences>,
    ) -> Result<(), Error> {
        let path = c_string(&path.as_ref().to_string_lossy())?;
        let preferences = preferences.map_or(std::ptr::null(), |p| p.handle.as_ptr().cast_const());
        // SAFETY: a live handle, a NUL-terminated path living for the call,
        // and a live or null preference handle, read only.
        let ok = unsafe { ffi::svz_open(self.raw(), path.as_ptr(), preferences) };
        if ok == 1 {
            Ok(())
        } else {
            Err(self.last_error())
        }
    }

    /// Applies `preferences`' entry for the open log's tag, or the defaults.
    pub fn apply_preferences(&mut self, preferences: &Preferences) -> Changes {
        // SAFETY: two live handles; the store is only read.
        Changes(unsafe { ffi::svz_apply_preferences(self.raw(), preferences.handle.as_ptr()) })
    }

    /// Stores the open log's display choices in `preferences` under its tag.
    pub fn remember_preferences(&self, preferences: &mut Preferences) {
        // SAFETY: two live handles; the session is only read.
        unsafe { ffi::svz_remember_preferences(self.raw(), preferences.handle.as_ptr()) }
    }

    /// Removes `preferences`' entry for the open log's tag.
    pub fn forget_preferences(&self, preferences: &mut Preferences) {
        // SAFETY: two live handles; the session is only read.
        unsafe { ffi::svz_forget_preferences(self.raw(), preferences.handle.as_ptr()) }
    }

    /// The open log.
    ///
    /// # Errors
    ///
    /// [`Error::Json`] for JSON this crate does not understand.
    pub fn catalog(&mut self) -> Result<Catalog, Error> {
        // SAFETY: a live handle.
        let text = unsafe { ffi::svz_catalog_json(self.raw()) };
        parse(&self.returned(text))
    }

    /// The view state.
    ///
    /// # Errors
    ///
    /// [`Error::Json`] for JSON this crate does not understand.
    pub fn view(&mut self) -> Result<View, Error> {
        // SAFETY: a live handle.
        let text = unsafe { ffi::svz_view_json(self.raw()) };
        parse(&self.returned(text))
    }

    /// The plot spec.
    ///
    /// # Errors
    ///
    /// [`Error::Json`] for JSON this crate does not understand.
    pub fn spec(&mut self) -> Result<Spec, Error> {
        // SAFETY: a live handle.
        let text = unsafe { ffi::svz_spec_json(self.raw()) };
        parse(&self.returned(text))
    }

    /// The JSON texts of the catalog, view and spec, as the library writes
    /// them: for comparing two sessions exactly.
    pub fn state_json(&mut self) -> [String; 3] {
        // SAFETY: a live handle; each text is copied before the next call.
        let catalog = unsafe { ffi::svz_catalog_json(self.raw()) };
        let catalog = self.returned(catalog);
        // SAFETY: as above.
        let view = unsafe { ffi::svz_view_json(self.raw()) };
        let view = self.returned(view);
        // SAFETY: as above.
        let spec = unsafe { ffi::svz_spec_json(self.raw()) };
        let spec = self.returned(spec);
        [catalog, view, spec]
    }

    /// The parameters of the transforms that apply to the open log, with
    /// their current values.
    ///
    /// # Errors
    ///
    /// [`Error::Json`] for JSON this crate does not understand.
    pub fn parameters(&mut self) -> Result<Vec<Parameter>, Error> {
        // SAFETY: a live handle.
        let text = unsafe { ffi::svz_parameters_json(self.raw()) };
        parse(&self.returned(text))
    }

    /// The hover readout at `time`: the time, then each visible stream's
    /// `label: value units`.
    ///
    /// # Errors
    ///
    /// [`Error::Json`] for JSON this crate does not understand.
    pub fn hover(&mut self, time: f64) -> Result<Vec<String>, Error> {
        // SAFETY: a live handle.
        let text = unsafe { ffi::svz_hover_json(self.raw(), time) };
        parse(&self.returned(text))
    }

    /// `time` as the readout shows it.
    pub fn format_time(&mut self, time: f64) -> String {
        // SAFETY: a live handle.
        let text = unsafe { ffi::svz_format_time(self.raw(), time) };
        self.returned(text)
    }

    /// The range the range dialog starts from for stream `id`; `None` for an
    /// unknown stream.
    pub fn range_for_dialog(&self, id: &str) -> Option<(f64, f64)> {
        let id = c_string(id).ok()?;
        let (mut lower, mut upper) = (0.0, 0.0);
        // SAFETY: a live handle, a NUL-terminated id, and two doubles to write.
        let ok =
            unsafe { ffi::svz_range_for_dialog(self.raw(), id.as_ptr(), &mut lower, &mut upper) };
        (ok == 1).then_some((lower, upper))
    }

    /// One stream's M4 envelope over `[t0, t1]` (infinities for the whole
    /// stream) at `columns`, breaking lines at gaps when `break_gaps`.
    pub fn envelope(
        &mut self,
        stream: u64,
        t0: f64,
        t1: f64,
        columns: i32,
        break_gaps: bool,
    ) -> Envelope {
        let call = |time: *mut f64, value: *mut f32, index: *mut u64, capacity: usize| {
            // SAFETY: a live handle; each array holds `capacity` elements (or
            // is null with capacity 0), and the library writes no more.
            unsafe {
                ffi::svz_envelope(
                    self.raw(),
                    stream,
                    t0,
                    t1,
                    columns,
                    c_int::from(break_gaps),
                    time,
                    value,
                    index,
                    capacity,
                )
            }
        };
        let count = call(
            std::ptr::null_mut(),
            std::ptr::null_mut(),
            std::ptr::null_mut(),
            0,
        );
        let mut time = vec![0.0; count];
        let mut value = vec![0.0f32; count];
        let mut index = vec![0u64; count];
        let written = call(
            time.as_mut_ptr(),
            value.as_mut_ptr(),
            index.as_mut_ptr(),
            count,
        );
        if written != count {
            return Envelope::default();
        }
        Envelope {
            time,
            value,
            index: index
                .into_iter()
                .map(|i| (i != ffi::SVZ_GAP_MARKER).then_some(i))
                .collect(),
        }
    }

    /// The compass orientation samples; empty without a compass.
    pub fn compass_samples(&mut self) -> Vec<OrientationSample> {
        // SAFETY: a live handle; null arrays with capacity 0 ask for the count.
        let count = unsafe {
            ffi::svz_compass_samples(self.raw(), std::ptr::null_mut(), std::ptr::null_mut(), 0)
        };
        let mut time = vec![0.0; count];
        let mut values = vec![0.0f32; 7 * count];
        // SAFETY: a live handle; `time` holds `count` and `values` 7 * `count`.
        let written = unsafe {
            ffi::svz_compass_samples(self.raw(), time.as_mut_ptr(), values.as_mut_ptr(), count)
        };
        if written != count {
            return Vec::new();
        }
        let (chunks, _) = values.as_chunks::<7>();
        time.iter()
            .zip(chunks)
            .map(|(&time, v)| OrientationSample {
                time,
                q: [v[0], v[1], v[2], v[3]],
                dip: v[4],
                field: v[5],
                mg: v[6],
            })
            .collect()
    }

    /// Takes the open log through sensorViz's state-dump script and returns
    /// the dump; it changes the session, so use one opened for the purpose.
    pub fn state_dump(&mut self) -> String {
        // SAFETY: a live handle.
        let text = unsafe { ffi::svz_state_dump(self.raw()) };
        self.returned(text)
    }

    /// Shows or hides stream `id`.
    pub fn set_stream_visible(&mut self, id: &str, visible: bool) -> Changes {
        let Ok(id) = c_string(id) else {
            return Changes::default();
        };
        // SAFETY: a live handle and a NUL-terminated id living for the call.
        Changes(unsafe {
            ffi::svz_set_stream_visible(self.raw(), id.as_ptr(), c_int::from(visible))
        })
    }

    /// Shows exactly the streams `ids`.
    ///
    /// # Errors
    ///
    /// [`Error::Nul`] when an id holds a NUL.
    pub fn set_visible_streams(&mut self, ids: &[&str]) -> Result<Changes, Error> {
        let (_owned, pointers) = c_strings(ids)?;
        // SAFETY: a live handle; `pointers` holds `ids.len()` NUL-terminated
        // strings, kept alive by `_owned` for the call.
        Ok(Changes(unsafe {
            ffi::svz_set_visible_streams(self.raw(), pointers.as_ptr(), pointers.len())
        }))
    }

    /// Sets a y range the user chose, and the ranges linked to it.
    pub fn set_stream_range(&mut self, id: &str, lower: f64, upper: f64) -> Changes {
        let Ok(id) = c_string(id) else {
            return Changes::default();
        };
        // SAFETY: a live handle and a NUL-terminated id living for the call.
        Changes(unsafe { ffi::svz_set_stream_range(self.raw(), id.as_ptr(), lower, upper) })
    }

    /// Reset Zoom: clears every custom range.
    pub fn reset_zoom(&mut self) -> Changes {
        // SAFETY: a live handle.
        Changes(unsafe { ffi::svz_reset_zoom(self.raw()) })
    }

    /// Replaces the color overrides: stream id and `0xRRGGBB`.
    ///
    /// # Errors
    ///
    /// [`Error::Nul`] when an id holds a NUL.
    pub fn set_colors(&mut self, colors: &[(&str, u32)]) -> Result<Changes, Error> {
        let ids: Vec<&str> = colors.iter().map(|(id, _)| *id).collect();
        let rgb: Vec<u32> = colors.iter().map(|(_, rgb)| *rgb).collect();
        let (_owned, pointers) = c_strings(&ids)?;
        // SAFETY: a live handle; both arrays hold `colors.len()` items, the
        // strings kept alive by `_owned` for the call.
        Ok(Changes(unsafe {
            ffi::svz_set_colors(self.raw(), pointers.as_ptr(), rgb.as_ptr(), rgb.len())
        }))
    }

    /// Replaces the axis-side overrides.
    ///
    /// # Errors
    ///
    /// [`Error::Nul`] when an id holds a NUL.
    pub fn set_axis_sides(&mut self, sides: &[(&str, Side)]) -> Result<Changes, Error> {
        let ids: Vec<&str> = sides.iter().map(|(id, _)| *id).collect();
        let right: Vec<c_int> = sides
            .iter()
            .map(|(_, side)| c_int::from(*side == Side::Right))
            .collect();
        let (_owned, pointers) = c_strings(&ids)?;
        // SAFETY: a live handle; both arrays hold `sides.len()` items, the
        // strings kept alive by `_owned` for the call.
        Ok(Changes(unsafe {
            ffi::svz_set_axis_sides(self.raw(), pointers.as_ptr(), right.as_ptr(), right.len())
        }))
    }

    /// Sets the title text.
    ///
    /// # Errors
    ///
    /// [`Error::Nul`] when the title holds a NUL.
    pub fn set_title(&mut self, title: &str) -> Result<Changes, Error> {
        let title = c_string(title)?;
        // SAFETY: a live handle and a NUL-terminated title living for the call.
        Ok(Changes(unsafe {
            ffi::svz_set_title(self.raw(), title.as_ptr())
        }))
    }

    /// Shows or hides the title.
    pub fn set_title_visible(&mut self, visible: bool) -> Changes {
        // SAFETY: a live handle.
        Changes(unsafe { ffi::svz_set_title_visible(self.raw(), c_int::from(visible)) })
    }

    /// Sets the UTC offset for epoch times, hours.
    pub fn set_utc_offset(&mut self, hours: i32) -> Changes {
        // SAFETY: a live handle.
        Changes(unsafe { ffi::svz_set_utc_offset(self.raw(), hours) })
    }

    /// Shows or hides the time cursors.
    pub fn set_cursors_visible(&mut self, visible: bool) -> Changes {
        // SAFETY: a live handle.
        Changes(unsafe { ffi::svz_set_cursors_visible(self.raw(), c_int::from(visible)) })
    }

    /// Breaks lines at gaps, or joins them.
    pub fn set_break_at_gaps(&mut self, break_gaps: bool) -> Changes {
        // SAFETY: a live handle.
        Changes(unsafe { ffi::svz_set_break_at_gaps(self.raw(), c_int::from(break_gaps)) })
    }

    /// Sets a transform parameter (an id from [`Session::parameters`]) and
    /// remakes what it affects; a toggle is on when `value` is nonzero.
    ///
    /// # Errors
    ///
    /// [`Error::Library`] when the transform failed to remake its outputs;
    /// [`Error::Nul`] when the id holds a NUL.
    pub fn set_parameter(&mut self, id: &str, value: f64) -> Result<Changes, Error> {
        let id = c_string(id)?;
        let mut changes = 0u32;
        // SAFETY: a live handle, a NUL-terminated id living for the call, and
        // one u32 to write.
        let ok = unsafe { ffi::svz_set_parameter(self.raw(), id.as_ptr(), value, &mut changes) };
        if ok == 1 {
            Ok(Changes(changes))
        } else {
            Err(self.last_error())
        }
    }
}

impl Drop for Session {
    fn drop(&mut self) {
        // SAFETY: the handle is live and freed only here.
        unsafe { ffi::svz_session_free(self.handle.as_ptr()) }
    }
}
