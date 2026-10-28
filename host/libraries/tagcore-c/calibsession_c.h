/**
 * @file    calibsession_c.h
 * @brief   C interface to the live magnetometer and accelerometer
 *          calibration (calibcore::CalibrationSession) and the compass
 *          display conversions.
 *
 * @details Wraps sensoranalysis's Qt-free calibration core
 *          (host/libraries/sensoranalysis/core/calibration_session.h) so a
 *          program in another language -- the Rust Tag Calibrator first --
 *          calibrates exactly as qtcalibrate does, through the same code:
 *          Freescale's solver, MagRetention's eviction, the accelerometer's
 *          own fit, MagQuality's figures, the filtered live compass, and the
 *          display angles every CompassTag display shows.
 *
 *          Vectors cross as three floats in the tag's axis order, quaternions
 *          as four floats scalar first (w, x, y, z), and figures as plain C
 *          structs copied out of the session, so nothing is converted on the
 *          way.
 *
 *          @code
 *          tcs_session *s = tcs_new();
 *          for (each sample in the calibration log) {
 *              if (tcs_add_sample(s, mag, has_accel, accel)) { ... new fit ... }
 *              tcs_orientation o;
 *              if (has_accel && tcs_orientation_of(s, mag, accel, &o)) { ... }
 *          }
 *          tcs_update_quality(s);             // on the caller's tick
 *          tcs_quality q; tcs_get_quality(s, &q);
 *          tcs_free(s);
 *          @endcode
 *
 *          Built into the same library as taglogwriter_c.h (libtagcore_c),
 *          which exports these functions beside the tlw_* ones.
 *
 * @note    A session is not thread-safe: use each handle from one thread at
 *          a time. Separate handles are independent, except that the solver
 *          counts its refit cadence across every session in the process
 *          (see calibration_session.h).
 */
#ifndef CALIBSESSION_C_H
#define CALIBSESSION_C_H

#include <stddef.h>

#include "taglogwriter_c.h" /* TLW_API */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @def     TCS_ABI_VERSION
 * @brief   Version of this interface; tcs_abi_version() returns the value the
 *          library was built with.
 *
 * @details Raised whenever a function's meaning, a signature or a struct
 *          layout changes, so a caller built against one header refuses a
 *          library built from another rather than misbehaving.
 */
#define TCS_ABI_VERSION 1u

/// Buffer slots in a session: tcs_points() never returns more.
#define TCS_SLOTS 650

/**
 * @brief   Opaque handle to one calibration session, from tcs_new() until
 *          tcs_free().
 */
typedef struct tcs_session tcs_session;

/**
 * @struct  tcs_quality
 * @brief   MagQuality's figures (calibcore::MagQuality::Result), field for
 *          field. See magquality_core.h for what each means.
 */
struct tcs_quality {
    int   patches_seen;        ///< Occupied magnetometer patches.
    int   mag_patches;         ///< Patches available.
    float coverage;            ///< patches_seen / mag_patches.
    int   patch_min;           ///< Samples in the emptiest occupied patch.
    int   patch_max;           ///< Samples in the fullest patch.
    float isotropy;            ///< Direction evenness, 0 to 1.
    float attitude_diversity;  ///< Gravity patches per occupied magnetometer patch.
    int   attitude_cells;      ///< Distinct (magnetometer, gravity) cells.
    int   attitude_patches;    ///< Magnetometer patches with a gravity observation.
    float field;               ///< Median calibrated magnitude, µT.
    float fit_error;           ///< RMS residual, percent of the field.
    float residual_spread;     ///< Robust residual spread, percent.
    float residual_p95;        ///< 95th percentile residual, percent.
    float residual_hard_iron;  ///< Hard iron the fit left, µT.
    float dip_mean_deg;        ///< Median inclination, degrees.
    float dip_spread_deg;      ///< Robust inclination spread, degrees.
    float dip_p95_deg;         ///< 95th percentile inclination deviation, degrees.
    int   dip_samples;         ///< Samples that passed the accelerometer gate.
    int   accel_samples;       ///< Samples offered with an accelerometer.
    int   have_accel_metrics;  ///< 1 once a sample passed the gate.
    int   samples;             ///< Samples accepted.
};

/// MagQuality's figures; see struct tcs_quality.
typedef struct tcs_quality tcs_quality;

/**
 * @struct  tcs_accel_fit
 * @brief   The accelerometer offset in force (calibcore::AccelCalibration::
 *          Result): the live fit, or one loaded with tcs_set_accel_offset().
 */
struct tcs_accel_fit {
    int   valid;          ///< 1: offset, radius and residual are a fit or loaded.
    float offset[3];      ///< Zero-g offset, mg.
    float radius;         ///< Fitted sphere radius, mg; 0 when loaded.
    float residual;       ///< RMS distance from the sphere, mg.
    int   samples;        ///< Readings the fit was built from.
    int   patches_seen;   ///< Occupied direction patches.
    int   patches;        ///< Patches in the lattice.
    float coverage;       ///< patches_seen / patches.
    float isotropy;       ///< Direction evenness, 0 to 1.
    int   offered;        ///< Readings offered since the last clear.
    int   gated;          ///< Of those, rejected as motion.
};

/// The accelerometer offset in force; see struct tcs_accel_fit.
typedef struct tcs_accel_fit tcs_accel_fit;

/**
 * @struct  tcs_orientation
 * @brief   One solved compass orientation (compasscore::Orientation).
 */
struct tcs_orientation {
    float q[4];   ///< Magnetic-frame orientation, w, x, y, z.
    float dip;    ///< Dip angle, degrees.
    float field;  ///< Field magnitude after calibration, µT.
    float mg;     ///< Acceleration magnitude, mg.
};

/// One solved orientation; see struct tcs_orientation.
typedef struct tcs_orientation tcs_orientation;

/**
 * @struct  tcs_angles
 * @brief   Display angles of an orientation (compasscore::DisplayAngles),
 *          before declination and battery direction.
 */
struct tcs_angles {
    double yaw;    ///< Magnetic heading, degrees clockwise, [0, 360).
    double pitch;  ///< About the tag x axis, degrees.
    double roll;   ///< About the tag y axis, degrees.
};

/// Display angles; see struct tcs_angles.
typedef struct tcs_angles tcs_angles;

/// The interface version the library was built with; compare with
/// TCS_ABI_VERSION before using anything else.
TLW_API unsigned tcs_abi_version(void);

/**
 * @brief   Creates an empty session.
 * @return  The handle, or NULL when memory is exhausted. Free with tcs_free().
 */
TLW_API tcs_session *tcs_new(void);

/// Frees a session; NULL is ignored.
TLW_API void tcs_free(tcs_session *s);

/**
 * @brief   Discards every sample, the fit, the accelerometer fit and any
 *          loaded offset; the field returns to 50 µT. The live compass filter
 *          is kept.
 */
TLW_API void tcs_clear(tcs_session *s);

/**
 * @brief   Feeds one calibration sample.
 *
 * @param[in] s          The session.
 * @param[in] mag        Raw magnetometer, µT, three floats.
 * @param[in] has_accel  Nonzero when @p accel is a reading.
 * @param[in] accel      Accelerometer, mg, three floats; may be NULL when
 *                       @p has_accel is zero.
 *
 * @return  1 when the solver accepted a new fit on this sample, else 0.
 */
TLW_API int tcs_add_sample(tcs_session *s, const float mag[3], int has_accel,
                           const float accel[3]);

/// 1 when a magnetometer calibration exists (fitted or loaded), else 0.
TLW_API int tcs_has_calibration(const tcs_session *s);

/**
 * @brief   Applies the current magnetometer calibration, A (mag - V).
 * @param[in]  s    The session.
 * @param[in]  mag  Raw magnetometer, µT.
 * @param[out] out  Calibrated, µT. The identity before a fit.
 */
TLW_API void tcs_calibrate(const tcs_session *s, const float mag[3], float out[3]);

/**
 * @brief   Copies the buffered readings, calibrated, in slot order.
 *
 * @param[in]  s      The session.
 * @param[out] xyz    Room for @p max points as x, y, z floats (3 * max);
 *                    may be NULL when @p max is 0.
 * @param[in]  max    Points @p xyz can hold.
 *
 * @return  The number of buffered readings, at most TCS_SLOTS, even when it
 *          exceeds @p max (only @p max are written).
 */
TLW_API size_t tcs_points(const tcs_session *s, float *xyz, size_t max);

/**
 * @brief   The current magnetometer constants.
 * @param[in]  s  The session.
 * @param[out] B  Field magnitude, µT.
 * @param[out] V  Hard-iron offset, µT, three floats.
 * @param[out] A  Inverse soft-iron matrix, nine floats, row-major.
 * @return  1 when a calibration exists; the outputs are written either way
 *          (the identity and 50 µT before a fit).
 */
TLW_API int tcs_constants(const tcs_session *s, float *B, float V[3], float A[9]);

/**
 * @brief   Installs magnetometer constants read back from a tag; the field is
 *          recomputed from the buffered readings, or kept when there are none.
 * @param[in] s  The session.
 * @param[in] V  Hard-iron offset, µT.
 * @param[in] A  Inverse soft-iron matrix, row-major.
 */
TLW_API void tcs_set_constants(tcs_session *s, const float V[3], const float A[9]);

/// The field magnitude B, µT.
TLW_API float tcs_field(const tcs_session *s);

/**
 * @brief   Installs an accelerometer offset read back from a tag, in mg. It
 *          stands until a live fit is valid or the session is cleared.
 */
TLW_API void tcs_set_accel_offset(tcs_session *s, const float offset[3]);

/// Recomputes the quality figures over the buffer, and the accelerometer fit.
TLW_API void tcs_update_quality(tcs_session *s);

/// Copies the quality figures as of the last tcs_update_quality().
TLW_API void tcs_get_quality(const tcs_session *s, tcs_quality *out);

/// Copies the accelerometer offset in force.
TLW_API void tcs_get_accel_fit(const tcs_session *s, tcs_accel_fit *out);

/**
 * @brief   Evictions since the last clear.
 * @param[in]  s    The session.
 * @param[out] out  Total, by leverage, as outliers.
 */
TLW_API void tcs_evictions(const tcs_session *s, int out[3]);

/**
 * @brief   Solves the live compass from one sample, through the session's
 *          low-pass filter, which this call advances.
 *
 * @param[in]  s      The session.
 * @param[in]  mag    Raw magnetometer, µT.
 * @param[in]  accel  Raw accelerometer, mg.
 * @param[out] out    The orientation.
 *
 * @return  1 on success; 0 when the filtered pair admits no orientation, and
 *          @p out is then unspecified.
 */
TLW_API int tcs_orientation_of(tcs_session *s, const float mag[3], const float accel[3],
                               tcs_orientation *out);

/**
 * @brief   Display angles of an orientation quaternion.
 * @param[in]  q    w, x, y, z.
 * @param[out] out  Heading, pitch and roll, degrees.
 */
TLW_API void tcs_display_angles(const float q[4], tcs_angles *out);

/**
 * @brief   The rotation qtcalibrate applies to its tag model:
 *          Rz(yaw) * Ry(roll) * Rx(pitch), each from Qt's fromAxisAndAngle()
 *          and multiplied with Qt's product.
 *
 * @param[in]  pitch  Degrees about x.
 * @param[in]  roll   Degrees about y.
 * @param[in]  yaw    Degrees about z, counter-clockwise.
 * @param[out] q      w, x, y, z.
 */
TLW_API void tcs_model_rotation(float pitch, float roll, float yaw, float q[4]);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // CALIBSESSION_C_H
