//! The raw C interface, as declared in `calibsession_c.h`.
//!
//! Hand-written: the safe wrapper, `calibsession`, says how to keep it in
//! step with the header ("Maintaining"). The header's Doxygen is the
//! contract.

use core::ffi::c_int;

/// The opaque C handle, `struct tcs_session`.
#[repr(C)]
pub struct TcsSession {
    /// Makes the type unconstructible in Rust.
    _private: [u8; 0],
}

/// `TCS_ABI_VERSION` from the header this crate was written against.
pub const TCS_ABI_VERSION: u32 = 1;

/// `tcs_quality`: MagQuality's figures.
#[repr(C)]
#[derive(Default)]
pub struct TcsQuality {
    /// Occupied magnetometer patches.
    pub patches_seen: c_int,
    /// Patches available.
    pub mag_patches: c_int,
    /// patches_seen / mag_patches.
    pub coverage: f32,
    /// Samples in the emptiest occupied patch.
    pub patch_min: c_int,
    /// Samples in the fullest patch.
    pub patch_max: c_int,
    /// Direction evenness.
    pub isotropy: f32,
    /// Gravity patches per occupied magnetometer patch.
    pub attitude_diversity: f32,
    /// Distinct (magnetometer, gravity) cells.
    pub attitude_cells: c_int,
    /// Magnetometer patches with a gravity observation.
    pub attitude_patches: c_int,
    /// Median calibrated magnitude, µT.
    pub field: f32,
    /// RMS residual, percent.
    pub fit_error: f32,
    /// Robust residual spread, percent.
    pub residual_spread: f32,
    /// 95th percentile residual, percent.
    pub residual_p95: f32,
    /// Hard iron left, µT.
    pub residual_hard_iron: f32,
    /// Median inclination, degrees.
    pub dip_mean_deg: f32,
    /// Robust inclination spread, degrees.
    pub dip_spread_deg: f32,
    /// 95th percentile inclination deviation, degrees.
    pub dip_p95_deg: f32,
    /// Samples that passed the gate.
    pub dip_samples: c_int,
    /// Samples with an accelerometer.
    pub accel_samples: c_int,
    /// 1 once a sample passed the gate.
    pub have_accel_metrics: c_int,
    /// Samples accepted.
    pub samples: c_int,
}

/// `tcs_accel_fit`: the accelerometer offset in force.
#[repr(C)]
#[derive(Default)]
pub struct TcsAccelFit {
    /// 1 when a fit or a loaded offset.
    pub valid: c_int,
    /// Zero-g offset, mg.
    pub offset: [f32; 3],
    /// Fitted radius, mg.
    pub radius: f32,
    /// RMS distance from the sphere, mg.
    pub residual: f32,
    /// Readings in the fit.
    pub samples: c_int,
    /// Occupied patches.
    pub patches_seen: c_int,
    /// Patches in the lattice.
    pub patches: c_int,
    /// patches_seen / patches.
    pub coverage: f32,
    /// Direction evenness.
    pub isotropy: f32,
    /// Readings offered.
    pub offered: c_int,
    /// Of those, rejected as motion.
    pub gated: c_int,
}

/// `tcs_orientation`: one solved orientation.
#[repr(C)]
#[derive(Default)]
pub struct TcsOrientation {
    /// w, x, y, z.
    pub q: [f32; 4],
    /// Dip, degrees.
    pub dip: f32,
    /// Field, µT.
    pub field: f32,
    /// Acceleration magnitude, mg.
    pub mg: f32,
}

/// `tcs_angles`: display angles.
#[repr(C)]
#[derive(Default)]
pub struct TcsAngles {
    /// Heading, degrees clockwise.
    pub yaw: f64,
    /// About x, degrees.
    pub pitch: f64,
    /// About y, degrees.
    pub roll: f64,
}

unsafe extern "C" {
    /// The interface version the library was built with.
    pub fn tcs_abi_version() -> u32;
    /// An empty session, or null when memory is exhausted.
    pub fn tcs_new() -> *mut TcsSession;
    /// Frees a session.
    pub fn tcs_free(s: *mut TcsSession);
    /// Discards the samples and fits.
    pub fn tcs_clear(s: *mut TcsSession);
    /// Feeds a sample; 1 when a new fit was accepted.
    pub fn tcs_add_sample(
        s: *mut TcsSession,
        mag: *const f32,
        has_accel: c_int,
        accel: *const f32,
    ) -> c_int;
    /// 1 when a calibration exists.
    pub fn tcs_has_calibration(s: *const TcsSession) -> c_int;
    /// Applies the calibration.
    pub fn tcs_calibrate(s: *const TcsSession, mag: *const f32, out: *mut f32);
    /// Copies up to `max` calibrated points; returns how many there are.
    pub fn tcs_points(s: *const TcsSession, xyz: *mut f32, max: usize) -> usize;
    /// The constants; 1 when a calibration exists.
    pub fn tcs_constants(s: *const TcsSession, b: *mut f32, v: *mut f32, a: *mut f32) -> c_int;
    /// Installs constants read from a tag.
    pub fn tcs_set_constants(s: *mut TcsSession, v: *const f32, a: *const f32);
    /// The field magnitude, µT.
    pub fn tcs_field(s: *const TcsSession) -> f32;
    /// Installs an accelerometer offset read from a tag.
    pub fn tcs_set_accel_offset(s: *mut TcsSession, offset: *const f32);
    /// Recomputes quality and the accelerometer fit.
    pub fn tcs_update_quality(s: *mut TcsSession);
    /// Copies the quality figures.
    pub fn tcs_get_quality(s: *const TcsSession, out: *mut TcsQuality);
    /// Copies the accelerometer fit.
    pub fn tcs_get_accel_fit(s: *const TcsSession, out: *mut TcsAccelFit);
    /// Evictions: total, by leverage, as outliers.
    pub fn tcs_evictions(s: *const TcsSession, out: *mut c_int);
    /// The filtered live compass; 1 on success.
    pub fn tcs_orientation_of(
        s: *mut TcsSession,
        mag: *const f32,
        accel: *const f32,
        out: *mut TcsOrientation,
    ) -> c_int;
    /// Display angles of a quaternion.
    pub fn tcs_display_angles(q: *const f32, out: *mut TcsAngles);
    /// qtcalibrate's tag-model rotation.
    pub fn tcs_model_rotation(pitch: f32, roll: f32, yaw: f32, q: *mut f32);
}
