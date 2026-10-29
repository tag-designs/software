//! The live tag calibration, and the compass angles every CompassTag display
//! shows, through the C++ core.
//!
//! [`Session`] is a safe wrapper over the C interface in
//! `host/libraries/tagcore-c/calibsession_c.h`, which wraps sensoranalysis's
//! Qt-free `calibcore::CalibrationSession` -- the same code qtcalibrate's
//! `CompassData` now runs. So a Rust program calibrates exactly as qtcalibrate
//! does: Freescale's solver, eviction by leverage and outliers, the
//! accelerometer's own fit, the quality figures and the filtered live compass.
//! [`display_angles`] and [`model_rotation`] are the conversions qtcalibrate's
//! displays apply to an orientation.
//!
//! ```no_run
//! use calibsession::Session;
//! # let samples: Vec<([f32; 3], Option<[f32; 3]>)> = vec![];
//! let mut session = Session::new()?;
//! for (i, (mag, accel)) in samples.iter().enumerate() {
//!     if session.add_sample(*mag, *accel) {
//!         // the solver accepted a new fit
//!     }
//!     if i % 2 == 1 {
//!         session.update_quality(); // the caller's tick
//!     }
//! }
//! let quality = session.quality();
//! # Ok::<(), calibsession::SessionError>(())
//! ```
//!
//! Units are the calibration stream's: magnetometer in µT, accelerometer in
//! mg. Quaternions are `[w, x, y, z]`.
//!
//! # Linking
//!
//! As for `taglogwriter`: `tagcore-c-sys` links `libtagcore_c`, found through
//! `TAGCORE_C_LIB_DIR`, statically on macOS and shared on Linux and Windows.
//! It is the one crate that declares `links = "tagcore_c"`, so a program may
//! use this crate beside the other wrappers over the library.
//!
//! # Safety model
//!
//! All `unsafe` is in this file. A [`Session`] owns exactly one C handle,
//! never null, freed exactly once in `Drop`. Every pointer passed in points
//! at Rust memory that lives until the call returns, sized as the header
//! requires; the library keeps no pointer after a call. Output structs are
//! `#[repr(C)]` mirrors of the header's, zero-initialized before the call
//! fills them. A session may move between threads but not be shared:
//! `Session` is `Send` and not `Sync`, and every call that advances it takes
//! `&mut self`.
//!
//! # Maintaining
//!
//! The declarations, imported here as `ffi`, are `tagcore_c_sys::tcs`
//! (`host/libraries/tagcore-c-sys/src/tcs.rs`), written by hand from
//! `calibsession_c.h`. When the
//! header changes, change the declaration and the safe method over it; if a
//! signature, meaning or struct layout changed, the header raises
//! `TCS_ABI_VERSION`, and `ffi::TCS_ABI_VERSION` must follow.
//! [`Session::new`] refuses a library built from another version.

use std::fmt;
use std::ptr::NonNull;

/// The raw C interface, in `tagcore-c-sys`, which links the library.
use tagcore_c_sys::tcs as ffi;


/// Buffer slots in a session: [`Session::points`] never returns more.
pub const SLOTS: usize = 650;

/// Why a session could not be made.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SessionError {
    /// The library was built from another version of `calibsession_c.h`.
    AbiMismatch {
        /// The version this crate expects.
        expected: u32,
        /// The version the library reports.
        found: u32,
    },
    /// The library could not allocate a session.
    OutOfMemory,
}

impl fmt::Display for SessionError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SessionError::AbiMismatch { expected, found } => write!(
                f,
                "libtagcore_c's calibration interface is version {found}, this program needs {expected}; \
                 rebuild them together"
            ),
            SessionError::OutOfMemory => write!(f, "out of memory creating a calibration session"),
        }
    }
}

impl std::error::Error for SessionError {}

/// The magnetometer calibration constants.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Constants {
    /// Whether a calibration exists (fitted or loaded). The values are the
    /// identity and 50 µT when it does not.
    pub valid: bool,
    /// Field magnitude B, µT.
    pub field: f32,
    /// Hard-iron offset V, µT.
    pub offset: [f32; 3],
    /// Inverse soft-iron matrix A, row-major.
    pub mapping: [[f32; 3]; 3],
}

/// MagQuality's figures; see `magquality_core.h` for what each means.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct Quality {
    /// Occupied magnetometer patches.
    pub patches_seen: i32,
    /// Patches available.
    pub mag_patches: i32,
    /// patches_seen / mag_patches.
    pub coverage: f32,
    /// Samples in the emptiest occupied patch.
    pub patch_min: i32,
    /// Samples in the fullest patch.
    pub patch_max: i32,
    /// Direction evenness, 0 to 1.
    pub isotropy: f32,
    /// Gravity patches per occupied magnetometer patch.
    pub attitude_diversity: f32,
    /// Distinct (magnetometer, gravity) cells.
    pub attitude_cells: i32,
    /// Magnetometer patches with a gravity observation.
    pub attitude_patches: i32,
    /// Median calibrated magnitude, µT.
    pub field: f32,
    /// RMS residual, percent of the field.
    pub fit_error: f32,
    /// Robust residual spread, percent of the field.
    pub residual_spread: f32,
    /// 95th percentile residual, percent of the field.
    pub residual_p95: f32,
    /// Hard iron the fit left, µT.
    pub residual_hard_iron: f32,
    /// Median inclination, degrees.
    pub dip_mean_deg: f32,
    /// Robust inclination spread, degrees.
    pub dip_spread_deg: f32,
    /// 95th percentile inclination deviation, degrees.
    pub dip_p95_deg: f32,
    /// Samples that passed the accelerometer gate.
    pub dip_samples: i32,
    /// Samples offered with an accelerometer.
    pub accel_samples: i32,
    /// Whether the dip figures exist.
    pub have_accel_metrics: bool,
    /// Samples accepted.
    pub samples: i32,
}

/// The accelerometer offset in force: the live fit, or one loaded from a tag.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct AccelFit {
    /// Whether offset, radius and residual are a fit or a loaded offset.
    pub valid: bool,
    /// Zero-g offset, mg.
    pub offset: [f32; 3],
    /// Fitted sphere radius, mg; 0 when loaded.
    pub radius: f32,
    /// RMS distance from the sphere, mg.
    pub residual: f32,
    /// Readings the fit was built from.
    pub samples: i32,
    /// Occupied direction patches.
    pub patches_seen: i32,
    /// Patches in the lattice.
    pub patches: i32,
    /// patches_seen / patches.
    pub coverage: f32,
    /// Direction evenness, 0 to 1.
    pub isotropy: f32,
    /// Readings offered since the last clear.
    pub offered: i32,
    /// Of those, rejected as motion.
    pub gated: i32,
}

/// Evictions from the full magnetometer buffer since the last clear.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Evictions {
    /// All evictions, including the random fallback's.
    pub total: i32,
    /// Decided as the least informative sample.
    pub leverage: i32,
    /// Decided as an outlier.
    pub outlier: i32,
}

/// One solved compass orientation.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct Orientation {
    /// Magnetic-frame orientation, `[w, x, y, z]`.
    pub q: [f32; 4],
    /// Dip angle, degrees.
    pub dip: f32,
    /// Field magnitude after calibration, µT.
    pub field: f32,
    /// Acceleration magnitude, mg.
    pub mg: f32,
}

/// Display angles of an orientation, before declination and battery direction.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct Angles {
    /// Magnetic heading, degrees clockwise from north, [0, 360).
    pub yaw: f64,
    /// About the tag x axis, degrees.
    pub pitch: f64,
    /// About the tag y axis, degrees.
    pub roll: f64,
}

/// One live calibration: the sample buffer, the solver, retention, the
/// accelerometer fit, the quality figures and the filtered compass.
pub struct Session {
    /// The C handle; never null, freed in `Drop`.
    ptr: NonNull<ffi::TcsSession>,
}

// SAFETY: the handle owns its memory and has no thread affinity; the C
// interface only forbids concurrent use, which `!Sync` and `&mut self`
// already prevent.
unsafe impl Send for Session {}

impl Session {
    /// Creates an empty session.
    ///
    /// # Errors
    ///
    /// [`SessionError::AbiMismatch`] when the linked library was built from
    /// another version of the header; [`SessionError::OutOfMemory`] when it
    /// cannot allocate.
    pub fn new() -> Result<Session, SessionError> {
        // SAFETY: takes nothing, returns a constant.
        let found = unsafe { ffi::tcs_abi_version() };
        if found != ffi::TCS_ABI_VERSION {
            return Err(SessionError::AbiMismatch {
                expected: ffi::TCS_ABI_VERSION,
                found,
            });
        }
        // SAFETY: takes nothing; returns an owned handle or null.
        let ptr = NonNull::new(unsafe { ffi::tcs_new() }).ok_or(SessionError::OutOfMemory)?;
        Ok(Session { ptr })
    }

    /// Discards every sample, the fit, the accelerometer fit and any loaded
    /// offset; the field returns to 50 µT. The live compass filter is kept.
    pub fn clear(&mut self) {
        // SAFETY: a live handle, used from one thread.
        unsafe { ffi::tcs_clear(self.ptr.as_ptr()) }
    }

    /// Feeds one calibration sample: raw magnetometer, µT, and the
    /// accelerometer reading, mg, when the sample has one.
    ///
    /// Returns true when the solver accepted a new fit on this sample.
    pub fn add_sample(&mut self, mag: [f32; 3], accel: Option<[f32; 3]>) -> bool {
        let a = accel.unwrap_or_default();
        // SAFETY: a live handle; both arrays hold three floats and outlive
        // the call.
        unsafe {
            ffi::tcs_add_sample(
                self.ptr.as_ptr(),
                mag.as_ptr(),
                accel.is_some().into(),
                a.as_ptr(),
            ) != 0
        }
    }

    /// Whether a magnetometer calibration exists (fitted or loaded).
    pub fn has_calibration(&self) -> bool {
        // SAFETY: a live handle.
        unsafe { ffi::tcs_has_calibration(self.ptr.as_ptr()) != 0 }
    }

    /// Applies the current magnetometer calibration, `A (mag - V)`; the
    /// identity before a fit.
    pub fn calibrate(&self, mag: [f32; 3]) -> [f32; 3] {
        let mut out = [0.0f32; 3];
        // SAFETY: a live handle; three floats in, room for three out.
        unsafe { ffi::tcs_calibrate(self.ptr.as_ptr(), mag.as_ptr(), out.as_mut_ptr()) };
        out
    }

    /// Every buffered reading, calibrated, in slot order; at most [`SLOTS`].
    pub fn points(&self) -> Vec<[f32; 3]> {
        let mut xyz = vec![0.0f32; 3 * SLOTS];
        // SAFETY: a live handle; `xyz` has room for SLOTS points, and the
        // library writes at most `max` of them.
        let n = unsafe { ffi::tcs_points(self.ptr.as_ptr(), xyz.as_mut_ptr(), SLOTS) }.min(SLOTS);
        xyz.as_chunks::<3>().0.iter().take(n).copied().collect()
    }

    /// The current magnetometer constants.
    pub fn constants(&self) -> Constants {
        let mut field = 0.0f32;
        let mut offset = [0.0f32; 3];
        let mut a = [0.0f32; 9];
        // SAFETY: a live handle; one float, three and nine of room.
        let valid = unsafe {
            ffi::tcs_constants(
                self.ptr.as_ptr(),
                &mut field,
                offset.as_mut_ptr(),
                a.as_mut_ptr(),
            )
        } != 0;
        Constants {
            valid,
            field,
            offset,
            mapping: [[a[0], a[1], a[2]], [a[3], a[4], a[5]], [a[6], a[7], a[8]]],
        }
    }

    /// Installs magnetometer constants read back from a tag. The field is
    /// recomputed from the buffered readings, or kept when there are none.
    pub fn set_constants(&mut self, offset: [f32; 3], mapping: [[f32; 3]; 3]) {
        let a: Vec<f32> = mapping.iter().flatten().copied().collect();
        // SAFETY: a live handle; three and nine floats that outlive the call.
        unsafe { ffi::tcs_set_constants(self.ptr.as_ptr(), offset.as_ptr(), a.as_ptr()) }
    }

    /// The field magnitude B, µT.
    pub fn field(&self) -> f32 {
        // SAFETY: a live handle.
        unsafe { ffi::tcs_field(self.ptr.as_ptr()) }
    }

    /// Installs an accelerometer offset read back from a tag, mg. It stands
    /// until a live fit is valid or the session is cleared.
    pub fn set_accel_offset(&mut self, offset: [f32; 3]) {
        // SAFETY: a live handle; three floats.
        unsafe { ffi::tcs_set_accel_offset(self.ptr.as_ptr(), offset.as_ptr()) }
    }

    /// Recomputes the quality figures and the accelerometer fit. A valid fit
    /// also tightens the accelerometer's intake gate, so how often this runs
    /// affects later samples: qtcalibrate's replay runs it every second
    /// sample.
    pub fn update_quality(&mut self) {
        // SAFETY: a live handle.
        unsafe { ffi::tcs_update_quality(self.ptr.as_ptr()) }
    }

    /// The quality figures as of the last [`Session::update_quality`].
    pub fn quality(&self) -> Quality {
        let mut q = ffi::TcsQuality::default();
        // SAFETY: a live handle; `q` is a repr(C) mirror of tcs_quality.
        unsafe { ffi::tcs_get_quality(self.ptr.as_ptr(), &mut q) };
        Quality {
            patches_seen: q.patches_seen,
            mag_patches: q.mag_patches,
            coverage: q.coverage,
            patch_min: q.patch_min,
            patch_max: q.patch_max,
            isotropy: q.isotropy,
            attitude_diversity: q.attitude_diversity,
            attitude_cells: q.attitude_cells,
            attitude_patches: q.attitude_patches,
            field: q.field,
            fit_error: q.fit_error,
            residual_spread: q.residual_spread,
            residual_p95: q.residual_p95,
            residual_hard_iron: q.residual_hard_iron,
            dip_mean_deg: q.dip_mean_deg,
            dip_spread_deg: q.dip_spread_deg,
            dip_p95_deg: q.dip_p95_deg,
            dip_samples: q.dip_samples,
            accel_samples: q.accel_samples,
            have_accel_metrics: q.have_accel_metrics != 0,
            samples: q.samples,
        }
    }

    /// The accelerometer offset in force.
    pub fn accel_fit(&self) -> AccelFit {
        let mut a = ffi::TcsAccelFit::default();
        // SAFETY: a live handle; `a` is a repr(C) mirror of tcs_accel_fit.
        unsafe { ffi::tcs_get_accel_fit(self.ptr.as_ptr(), &mut a) };
        AccelFit {
            valid: a.valid != 0,
            offset: a.offset,
            radius: a.radius,
            residual: a.residual,
            samples: a.samples,
            patches_seen: a.patches_seen,
            patches: a.patches,
            coverage: a.coverage,
            isotropy: a.isotropy,
            offered: a.offered,
            gated: a.gated,
        }
    }

    /// Evictions since the last clear.
    pub fn evictions(&self) -> Evictions {
        let mut out = [0; 3];
        // SAFETY: a live handle; room for three ints.
        unsafe { ffi::tcs_evictions(self.ptr.as_ptr(), out.as_mut_ptr()) };
        Evictions {
            total: out[0],
            leverage: out[1],
            outlier: out[2],
        }
    }

    /// Solves the live compass from one raw sample, through the session's
    /// low-pass filter, which this advances.
    ///
    /// Returns `None` when the filtered pair admits no orientation.
    pub fn orientation(&mut self, mag: [f32; 3], accel: [f32; 3]) -> Option<Orientation> {
        let mut o = ffi::TcsOrientation::default();
        // SAFETY: a live handle; three floats each in; `o` is a repr(C)
        // mirror of tcs_orientation.
        let ok = unsafe {
            ffi::tcs_orientation_of(self.ptr.as_ptr(), mag.as_ptr(), accel.as_ptr(), &mut o)
        } != 0;
        ok.then_some(Orientation {
            q: o.q,
            dip: o.dip,
            field: o.field,
            mg: o.mg,
        })
    }
}

impl Drop for Session {
    fn drop(&mut self) {
        // SAFETY: the handle is live and owned by this value alone; it is
        // freed exactly once, here.
        unsafe { ffi::tcs_free(self.ptr.as_ptr()) }
    }
}

/// Display angles of an orientation quaternion `[w, x, y, z]`: the heading,
/// pitch and roll every CompassTag display shows, before declination and
/// battery direction.
pub fn display_angles(q: [f32; 4]) -> Angles {
    let mut a = ffi::TcsAngles::default();
    // SAFETY: four floats in; `a` is a repr(C) mirror of tcs_angles.
    unsafe { ffi::tcs_display_angles(q.as_ptr(), &mut a) };
    Angles {
        yaw: a.yaw,
        pitch: a.pitch,
        roll: a.roll,
    }
}

/// The rotation qtcalibrate applies to its tag model, `Rz(yaw) * Ry(roll) *
/// Rx(pitch)`, degrees, yaw counter-clockwise; `[w, x, y, z]`.
pub fn model_rotation(pitch: f32, roll: f32, yaw: f32) -> [f32; 4] {
    let mut q = [0.0f32; 4];
    // SAFETY: room for four floats.
    unsafe { ffi::tcs_model_rotation(pitch, roll, yaw, q.as_mut_ptr()) };
    q
}
