/**
 * @file    compass_core.h
 * @brief   Qt-free compass calibration and eCompass orientation solve.
 *
 * @details The arithmetic behind CompassCalibration and CompassProcessor,
 *          with no Qt dependency so that command-line tools can link it. The
 *          Qt classes in sensoranalysis are thin adapters over these
 *          functions, so there is one implementation of the math.
 *
 *          Results are bit-identical to the Qt implementation this replaced, as
 *          macOS built it: the code is a line-for-line port onto sensormath
 *          (vecmath.h), whose types reproduce QVector3D and QQuaternion
 *          exactly. compass_core_parity_check verifies that against a
 *          verbatim copy of the Qt code.
 *
 *          The Qt code was not consistent across platforms: it called sqrt()
 *          and atan2() unqualified on floats, which GNU libstdc++ resolves to
 *          the double C functions and Apple's libc++ to the float overloads.
 *          The core calls std::sqrt and std::atan2, so it computes as macOS
 *          always did, on every platform; against the Qt code as Linux built
 *          it, it differs by a few float ulps.
 *
 * @see     vecmath.h, CompassProcessor, CompassCalibration
 */
#ifndef SENSORANALYSIS_CORE_COMPASS_CORE_H
#define SENSORANALYSIS_CORE_COMPASS_CORE_H

#include "vecmath.h"

#include <functional>

namespace compasscore
{

/**
 * @struct  CalibrationConstants
 * @brief   Magnetometer and accelerometer calibration for one tag.
 *
 * @details The default value is the identity calibration: zero hard iron,
 *          identity soft iron, zero accelerometer offset.
 */
struct CalibrationConstants
{
    sensormath::Vec3f hardIron;  ///< Hard-iron offset, uT, subtracted first.
    sensormath::Mat3d softIron{{{{1.0, 0.0, 0.0}},
                                {{0.0, 1.0, 0.0}},
                                {{0.0, 0.0, 1.0}}}}; ///< Soft-iron correction, row-major.
    sensormath::Vec3f accelOffset; ///< Accelerometer zero-g offset, mg.
};

/**
 * @brief   Reads one number from a stored calibration document.
 *
 * @details Called with a section -- "magnetometer" or "accelerometer" -- and
 *          a key within it. Returns the number stored there as a double, or
 *          @p fallback when the section or the key is absent or is not a
 *          number. Callers adapt their JSON library to this; the core needs
 *          none.
 */
using CalibrationLookup =
    std::function<double(const char *section, const char *key, double fallback)>;

/**
 * @brief   Builds calibration constants from a stored CalibrationConstants
 *          document.
 *
 * @details The document is the proto3 JSON of tagdata.proto
 *          CalibrationConstants, as the download path stores it in the
 *          Calibration table: magnetometer v0..v2 (hard iron, uT) and
 *          a00..a22 (soft iron), accelerometer o0..o2 (zero-g offset, mg).
 *          proto3 JSON omits zero-valued fields, so a missing key reads as
 *          zero -- except a00, a11 and a22, which default to one so that a
 *          document with no magnetometer section is the identity. A log
 *          written before the accelerometer offset existed has no
 *          accelerometer section and gets a zero offset.
 *
 *          The numbers must reach this function as the doubles parsed from
 *          the JSON text, not as the float protobuf fields: soft iron is used
 *          in double, and a float widened to double is a different number
 *          from the text's own double. Hard iron and the accelerometer offset
 *          are rounded to float here.
 *
 * @param[in] lookup Reads one number; see CalibrationLookup.
 *
 * @return  The calibration the document describes.
 *
 * @see     calibrationFromJson() in calibration_json.h for the protobuf-based
 *          reader used without Qt; CompassCalibration::fromCalibrationJson()
 *          for the Qt one.
 */
CalibrationConstants calibrationFromLookup(const CalibrationLookup &lookup);

/**
 * @struct  Orientation
 * @brief   Result of solving one accelerometer/magnetometer pair.
 */
struct Orientation
{
    sensormath::Quatf q; ///< Magnetic-frame orientation quaternion.
    float dip = 0.0f;    ///< Magnetic dip angle, degrees.
    float field = 0.0f;  ///< Magnetic field magnitude, after calibration when applied, uT.
    float mg = 0.0f;     ///< Acceleration magnitude, mg.
};

/**
 * @brief   Applies hard-iron and soft-iron correction to a magnetometer vector.
 *
 * @details Subtracts the hard-iron offset in float, then multiplies by the
 *          soft-iron matrix in double, and rounds the result to float.
 *
 * @param[in] constants Calibration to apply.
 * @param[in] mag       Raw magnetometer vector, uT.
 *
 * @return  Calibrated magnetometer vector, uT.
 */
sensormath::Vec3f applyMagnetometer(const CalibrationConstants &constants, sensormath::Vec3f mag);

/**
 * @brief   Subtracts the accelerometer zero-g offset.
 *
 * @param[in] constants Calibration holding the offset.
 * @param[in] accel     Raw accelerometer vector, mg.
 *
 * @return  Offset-corrected accelerometer vector, mg.
 */
sensormath::Vec3f applyAccelerometer(const CalibrationConstants &constants, sensormath::Vec3f accel);

/**
 * @brief   Solves magnetic-frame orientation from one sensor pair.
 *
 * @details Quaternion eCompass after R. G. Valenti, I. Dryanovski and J. Xiao,
 *          "Keeping a Good Attitude: A Quaternion-Based Orientation Filter for
 *          IMUs and MARGs", Sensors 15(8), 2015, doi:10.3390/s150819302, in a
 *          north-west-up frame. The accelerometer vector is used
 *          as given; the magnetometer is calibrated first when @p constants is
 *          not null. The sensor axes are relabelled into NWU by the sign flips
 *          and the q2/q1 ordering inside; together they are one signed axis
 *          permutation of the magnetometer onto the accelerometer (see
 *          docs/shared/sensor-axes.md), and must change together if at all.
 *
 * @param[in]  mag        Magnetometer vector, uT: raw when @p constants is
 *                        given, already calibrated otherwise.
 * @param[in]  accel      Accelerometer vector, mg, offset already removed.
 * @param[in]  constants  Calibration to apply to @p mag, or nullptr when the
 *                        caller has already calibrated it.
 * @param[out] result     Orientation, dip, field and acceleration magnitude.
 *                        Left unchanged on failure.
 *
 * @return  true on success; false when either vector has zero length after
 *          calibration, in which case no orientation exists.
 */
bool solveOrientation(
    sensormath::Vec3f mag,
    sensormath::Vec3f accel,
    const CalibrationConstants *constants,
    Orientation &result);

/**
 * @struct  DisplayAngles
 * @brief   Heading, pitch and roll of one compass orientation, in degrees.
 *
 * @details Magnetic-frame values. Declination and the battery direction are
 *          display choices applied afterwards.
 */
struct DisplayAngles
{
    double yaw = 0.0;   ///< Magnetic yaw, degrees clockwise from magnetic north, [0, 360).
    double pitch = 0.0; ///< Rotation about the tag x axis, degrees, [-90, 90].
    double roll = 0.0;  ///< Rotation about the tag y axis, degrees, (-180, 180].
};

/**
 * @brief   Converts an orientation quaternion to display angles.
 *
 * @details The convention every CompassTag display has used: Qt's
 *          QQuaternion::toEulerAngles() (sensormath::toEulerAngles()), whose
 *          angles are about x, y and z for q = Ry * Rx * Rz, relabelled to the
 *          tag -- the x angle is pitch, the y angle roll, and the z angle,
 *          reversed to increase clockwise, yaw. It is not the aerospace Z-Y-X
 *          decomposition, and the two disagree when the tag is tilted about
 *          both horizontal axes.
 *
 *          The arithmetic is sensorui's compassAnglesFromQuaternion(), now an
 *          adapter over this, step for step: the z angle is wrapped into
 *          [0, 360) in double and stored as float, and the yaw is fmod in
 *          double of a float difference. Bit-identical to it
 *          (compass_core_parity_check).
 *
 * @param[in] q  Orientation quaternion as solveOrientation() returns it; need
 *               not be unit. The identity yields all-zero angles.
 *
 * @return  The angles, degrees.
 */
DisplayAngles displayAngles(const sensormath::Quatf &q);

} // namespace compasscore

#endif // SENSORANALYSIS_CORE_COMPASS_CORE_H
