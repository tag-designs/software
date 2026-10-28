#ifndef COMPASS_TYPES_H
#define COMPASS_TYPES_H

#include "compass_core.h"

#include <QJsonObject>
#include <QQuaternion>
#include <QVector>
#include <QVector3D>

#include <array>

// Exact conversions between the Qt types used by this library's interface and
// the sensormath types of the Qt-free core. Both sides store floats, so these
// copy bits and never round.
inline sensormath::Vec3f toVec3f(const QVector3D &v)
{
    return sensormath::Vec3f{v.x(), v.y(), v.z()};
}

inline QVector3D toQVector3D(const sensormath::Vec3f &v)
{
    return QVector3D(v.x, v.y, v.z);
}

inline QQuaternion toQQuaternion(const sensormath::Quatf &q)
{
    return QQuaternion(q.w, q.x, q.y, q.z);
}

inline sensormath::Quatf toQuatf(const QQuaternion &q)
{
    return sensormath::Quatf{q.scalar(), q.x(), q.y(), q.z()};
}

/*
 * Compass data moves through three layers:
 *
 * - CompassCalibration stores the hard-iron vector and soft-iron matrix, and
 *   the accelerometer zero-g offset, read from the Calibration table.
 * - CompassRawSample is the direct row-level accelerometer/magnetometer data
 *   read from the Compass table.
 * - CompassDerivedSample is the magnetic-frame orientation computed from one
 *   raw row, as a quaternion. It carries no Euler angles: heading, pitch and
 *   roll, and display choices such as declination and battery direction, are
 *   derived by the UI (sensorui compassAnglesFromQuaternion()).
 */

class CompassCalibration
{
public:
    using Matrix = std::array<std::array<double, 3>, 3>;

    CompassCalibration();

    /// Reads a whole CalibrationConstants JSON object: "magnetometer" and,
    /// when present, "accelerometer". A log written before the accelerometer
    /// offset existed has none, and gets a zero offset.
    static CompassCalibration fromCalibrationJson(const QJsonObject &root);
    static CompassCalibration fromMagnetometerJson(const QJsonObject &constants);
    static CompassCalibration fromMagnetometerConstants(
        const QVector3D &hardIron,
        const Matrix &softIron);
    /// Wraps calibration constants from the Qt-free core; exact.
    static CompassCalibration fromConstants(const compasscore::CalibrationConstants &constants);

    /// Hard- and soft-iron correction; compasscore::applyMagnetometer().
    QVector3D apply(const QVector3D &mag) const;

    /// Subtracts the accelerometer zero-g offset. Both are in mg.
    QVector3D applyAccel(const QVector3D &accel) const;

    QVector3D hardIron() const;
    const Matrix &softIron() const;

    /// Accelerometer zero-g offset, mg. Zero when none was recorded.
    QVector3D accelOffset() const;
    void setAccelOffset(const QVector3D &offset);

    /// The same calibration as the Qt-free core's constants.
    compasscore::CalibrationConstants constants() const;

private:
    QVector3D hardIron_;
    Matrix softIron_;
    QVector3D accelOffset_;
};

struct CompassRawSample
{
    double epoch = 0.0;
    QVector3D accel;
    QVector3D mag;
};

struct CompassDerivedSample
{
    double epoch = 0.0;
    QVector3D accel;
    QVector3D mag;
    QQuaternion q;
    float dip = 0.0f;
    float field = 0.0f;
    float mg = 0.0f;
};

struct CompassLogData
{
    CompassCalibration calibration;
    QVector<CompassDerivedSample> orientation;
    QVector<double> orientationTime;
    QVector<double> acceleration;
};

#endif // COMPASS_TYPES_H
