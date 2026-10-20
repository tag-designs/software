/**
 * @file    compass_core.h
 * @brief   Qt-free compass calibration and eCompass orientation solve.
 *
 * @details The arithmetic behind CompassCalibration and CompassProcessor,
 *          with no Qt dependency so that command-line tools can link it. The
 *          Qt classes in sensoranalysis are thin adapters over these
 *          functions, so there is one implementation of the math.
 *
 *          Results are bit-identical to the Qt implementation this replaced:
 *          the code is a line-for-line port onto sensormath (vecmath.h),
 *          whose types reproduce QVector3D and QQuaternion exactly.
 *          compass_core_parity_check verifies that against a verbatim copy of
 *          the Qt code.
 *
 * @see     vecmath.h, CompassProcessor, CompassCalibration
 */
#ifndef SENSORANALYSIS_CORE_COMPASS_CORE_H
#define SENSORANALYSIS_CORE_COMPASS_CORE_H

#include "vecmath.h"

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

} // namespace compasscore

#endif // SENSORANALYSIS_CORE_COMPASS_CORE_H
