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
                 {{0.0, 0.0, 1.0}}}}
{
}

CompassCalibration CompassCalibration::fromMagnetometerJson(const QJsonObject &constants)
{
    CompassCalibration calibration;

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

QVector3D CompassCalibration::apply(const QVector3D &mag) const
{
    const double x = mag.x() - hardIron_.x();
    const double y = mag.y() - hardIron_.y();
    const double z = mag.z() - hardIron_.z();

    return QVector3D(
        x * softIron_[0][0] + y * softIron_[0][1] + z * softIron_[0][2],
        x * softIron_[1][0] + y * softIron_[1][1] + z * softIron_[1][2],
        x * softIron_[2][0] + y * softIron_[2][1] + z * softIron_[2][2]);
}

QVector3D CompassCalibration::hardIron() const
{
    return hardIron_;
}

const CompassCalibration::Matrix &CompassCalibration::softIron() const
{
    return softIron_;
}
