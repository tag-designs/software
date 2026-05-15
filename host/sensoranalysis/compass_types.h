#ifndef COMPASS_TYPES_H
#define COMPASS_TYPES_H

#include <QJsonObject>
#include <QQuaternion>
#include <QVector>
#include <QVector3D>

#include <array>

/*
 * Compass data moves through three layers:
 *
 * - CompassCalibration stores the hard-iron vector and soft-iron matrix read
 *   from the Calibration table.
 * - CompassRawSample is the direct row-level accelerometer/magnetometer data
 *   read from the Compass table.
 * - CompassDerivedSample is the magnetic-frame orientation computed from one
 *   raw row. User display choices such as declination and battery direction
 *   are applied later by the UI when drawing heading.
 */

class CompassCalibration
{
public:
    using Matrix = std::array<std::array<double, 3>, 3>;

    CompassCalibration();

    static CompassCalibration fromMagnetometerJson(const QJsonObject &constants);

    QVector3D apply(const QVector3D &mag) const;

    QVector3D hardIron() const;
    const Matrix &softIron() const;

private:
    QVector3D hardIron_;
    Matrix softIron_;
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
    float pitch = 0.0f;
    float roll = 0.0f;
    float yaw = 0.0f;
};

struct CompassLogData
{
    CompassCalibration calibration;
    QVector<CompassDerivedSample> orientation;
    QVector<double> orientationTime;
    QVector<double> acceleration;
};

#endif // COMPASS_TYPES_H
