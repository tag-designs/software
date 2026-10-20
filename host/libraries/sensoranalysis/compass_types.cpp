#include "compass_types.h"

namespace
{

double jsonDouble(const QJsonObject &object, const char *key, double fallback)
{
    return object.value(QString::fromLatin1(key)).toDouble(fallback);
}

} // namespace

CompassCalibration::CompassCalibration()
    : hardIron_(0.0f, 0.0f, 0.0f),
      softIron_{{{{1.0, 0.0, 0.0}},
                 {{0.0, 1.0, 0.0}},
                 {{0.0, 0.0, 1.0}}}},
      accelOffset_(0.0f, 0.0f, 0.0f)
{
}

CompassCalibration CompassCalibration::fromCalibrationJson(const QJsonObject &root)
{
    CompassCalibration calibration = fromMagnetometerJson(
        root.value(QStringLiteral("magnetometer")).toObject());

    // proto3 JSON omits zero-valued fields, so a missing key is a zero.
    const QJsonObject accel = root.value(QStringLiteral("accelerometer")).toObject();
    calibration.accelOffset_ = QVector3D(
        jsonDouble(accel, "o0", 0.0),
        jsonDouble(accel, "o1", 0.0),
        jsonDouble(accel, "o2", 0.0));
    return calibration;
}

CompassCalibration CompassCalibration::fromMagnetometerJson(const QJsonObject &constants)
{
    CompassCalibration calibration;

    // The embedded calibration JSON uses historical names: v* is the hard-iron
    // offset vector, and a** is the soft-iron correction matrix.
    calibration.hardIron_ = QVector3D(
        jsonDouble(constants, "v0", 0.0),
        jsonDouble(constants, "v1", 0.0),
        jsonDouble(constants, "v2", 0.0));

    calibration.softIron_ = {{{{jsonDouble(constants, "a00", 1.0),
                                jsonDouble(constants, "a01", 0.0),
                                jsonDouble(constants, "a02", 0.0)}},
                              {{jsonDouble(constants, "a10", 0.0),
                                jsonDouble(constants, "a11", 1.0),
                                jsonDouble(constants, "a12", 0.0)}},
                              {{jsonDouble(constants, "a20", 0.0),
                                jsonDouble(constants, "a21", 0.0),
                                jsonDouble(constants, "a22", 1.0)}}}};

    return calibration;
}

CompassCalibration CompassCalibration::fromMagnetometerConstants(
    const QVector3D &hardIron,
    const Matrix &softIron)
{
    CompassCalibration calibration;
    calibration.hardIron_ = hardIron;
    calibration.softIron_ = softIron;
    return calibration;
}

QVector3D CompassCalibration::apply(const QVector3D &mag) const
{
    return toQVector3D(compasscore::applyMagnetometer(constants(), toVec3f(mag)));
}

QVector3D CompassCalibration::applyAccel(const QVector3D &accel) const
{
    return toQVector3D(compasscore::applyAccelerometer(constants(), toVec3f(accel)));
}

compasscore::CalibrationConstants CompassCalibration::constants() const
{
    compasscore::CalibrationConstants constants;
    constants.hardIron = toVec3f(hardIron_);
    constants.softIron = softIron_;
    constants.accelOffset = toVec3f(accelOffset_);
    return constants;
}

QVector3D CompassCalibration::hardIron() const
{
    return hardIron_;
}

const CompassCalibration::Matrix &CompassCalibration::softIron() const
{
    return softIron_;
}

QVector3D CompassCalibration::accelOffset() const
{
    return accelOffset_;
}

void CompassCalibration::setAccelOffset(const QVector3D &offset)
{
    accelOffset_ = offset;
}
