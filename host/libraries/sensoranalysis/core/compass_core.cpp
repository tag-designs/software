/**
 * @file    compass_core.cpp
 * @brief   Qt-free compass calibration and eCompass orientation solve.
 *
 * @details A line-for-line port of the QVector3D/QQuaternion implementation
 *          that CompassProcessor and CompassCalibration used to hold. Mixed
 *          float and double expressions are kept exactly as they were, since
 *          each conversion point is part of the rounding the output depends
 *          on; do not "tidy" them.
 */
#include "compass_core.h"

#include <cmath>

namespace compasscore
{

using sensormath::Quatf;
using sensormath::Vec3f;

namespace
{

/// The value of M_PI, which is not standard C++.
constexpr double kPi = 3.14159265358979323846;

} // namespace

Vec3f applyMagnetometer(const CalibrationConstants &constants, Vec3f mag)
{
    // The difference is taken in float and widened, as QVector3D components
    // are; the matrix product is double and rounds once to float.
    const double x = mag.x - constants.hardIron.x;
    const double y = mag.y - constants.hardIron.y;
    const double z = mag.z - constants.hardIron.z;
    const sensormath::Mat3d &s = constants.softIron;

    return Vec3f{
        static_cast<float>(x * s[0][0] + y * s[0][1] + z * s[0][2]),
        static_cast<float>(x * s[1][0] + y * s[1][1] + z * s[1][2]),
        static_cast<float>(x * s[2][0] + y * s[2][1] + z * s[2][2])};
}

Vec3f applyAccelerometer(const CalibrationConstants &constants, Vec3f accel)
{
    return accel - constants.accelOffset;
}

/*
 * Quaternion eCompass implementation based on:
 *
 *   Keeping a Good Attitude: A Quaternion-Based Orientation Filter for IMUs
 *   and MARGs
 *   Roberto G. Valenti, Ivan Dryanovski, Jizhong Xiao
 *   Sensors 2015, 15(8), 19302-19330, doi:10.3390/s150819302
 *
 * The algorithm assumes a north-west-up global frame. qacc is the paper's
 * accelerometer-only tilt quaternion (its two branches avoid the singularity
 * at accel.z = -1), and qmag the heading quaternion about the vertical that
 * aligns the tilt-corrected magnetometer with north (its two branches avoid
 * the singularity when that vector points south). The code is kept close to
 * the original compviz implementation, which the port onto sensormath has not
 * changed: the results are bit-identical.
 */
bool solveOrientation(
    Vec3f mag,
    Vec3f accel,
    const CalibrationConstants *constants,
    Orientation &result)
{
    float q0, q1, q2, q3;

    if (constants) {
        mag = applyMagnetometer(*constants, mag);
    }

    if ((sensormath::length(mag) == 0.0) || (sensormath::length(accel) == 0.0)) {
        return false;
    }

    const float field = sensormath::length(mag);
    const float mg = sensormath::length(accel);
    sensormath::normalize(accel);
    sensormath::normalize(mag);

    // Switch to the NWU convention used by the orientation algorithm. These
    // two flips and the q2/q1 ordering of qacc below are one unit: together
    // they are the +y,-x,-z relabelling of the magnetometer onto the
    // accelerometer (docs/shared/sensor-axes.md).
    accel = Vec3f{accel.x, -accel.y, accel.z};
    mag = Vec3f{mag.x, mag.y, -mag.z};

    if (accel.z >= 0) {
        q0 = std::sqrt((accel.z + 1) * 0.5);
        q1 = -accel.y / (2.0 * q0);
        q2 = accel.x / (2.0 * q0);
        q3 = 0;
    } else {
        double x = std::sqrt((1 - accel.z) * 0.5);
        q0 = -accel.y / (2.0 * x);
        q1 = x;
        q2 = 0;
        q3 = accel.x / (2.0 * x);
    }

    Quatf qacc{q0, q2, q1, q3};
    sensormath::normalize(qacc);

    const Vec3f rotatedMag = sensormath::rotate(qacc, mag);

    float gamma = rotatedMag.x * rotatedMag.x + rotatedMag.y * rotatedMag.y;
    float beta = std::sqrt(gamma + rotatedMag.x * std::sqrt(gamma));
    float betaneg = std::sqrt(gamma - rotatedMag.x * std::sqrt(gamma));
    float q0_mag, q3_mag;

    if (rotatedMag.x >= 0.0) {
        q0_mag = beta / std::sqrt(2.0 * gamma);
        q3_mag = rotatedMag.y / (std::sqrt(2.0) * beta);
    } else {
        q0_mag = rotatedMag.y / (std::sqrt(2.0) * betaneg);
        q3_mag = betaneg / std::sqrt(2.0 * gamma);
    }

    Quatf qmag{q0_mag, 0, 0, -q3_mag};
    qmag = Quatf{static_cast<float>(std::sqrt(2) / 2), 0, 0, static_cast<float>(std::sqrt(2) / 2)}
        * qmag;
    sensormath::normalize(qmag);

    Quatf q = qacc * qmag;
    sensormath::normalize(q);
    q.x = -q.x;
    q.y = -q.y;

    result.q = q;
    result.field = field;
    result.mg = mg;
    result.dip = std::atan2(rotatedMag.z, std::sqrt(gamma)) * 180.0 / kPi;
    return true;
}

} // namespace compasscore
