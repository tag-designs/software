#include "compass_types.h"

namespace
{

/// QJsonObject adapter for compasscore::CalibrationLookup. @p sectionOf picks
/// the object a section name refers to; a missing or non-object section reads
/// as empty, so every key in it falls back.
template <typename SectionOf>
compasscore::CalibrationLookup jsonLookup(SectionOf sectionOf)
{
    return [sectionOf](const char *section, const char *key, double fallback) {
        return sectionOf(section).value(QString::fromLatin1(key)).toDouble(fallback);
    };
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
    // The field mapping and defaults are the core's, shared with the Qt-free
    // reader in core/calibration_json.h.
    return fromConstants(compasscore::calibrationFromLookup(jsonLookup(
        [&root](const char *section) {
            return root.value(QString::fromLatin1(section)).toObject();
        })));
}

CompassCalibration CompassCalibration::fromMagnetometerJson(const QJsonObject &constants)
{
    // A bare magnetometer object: its keys answer the "magnetometer" section,
    // and the accelerometer section is absent, so the offset is zero.
    return fromConstants(compasscore::calibrationFromLookup(jsonLookup(
        [&constants](const char *section) {
            return QString::fromLatin1(section) == QLatin1String("magnetometer")
                ? constants
                : QJsonObject();
        })));
}

CompassCalibration CompassCalibration::fromConstants(const compasscore::CalibrationConstants &constants)
{
    CompassCalibration calibration;
    calibration.hardIron_ = toQVector3D(constants.hardIron);
    calibration.softIron_ = constants.softIron;
    calibration.accelOffset_ = toQVector3D(constants.accelOffset);
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
