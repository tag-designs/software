/**
 * @file    magquality.cpp
 * @brief   Qt adapter over calibcore::MagQuality.
 */
#include "magquality.h"

#include "compass_types.h"
#include "directions.h"

MagQuality::MagQuality()
    : core_()
{
}

MagQuality::MagQuality(const Config &config)
    : core_(config)
{
}

void MagQuality::reset()
{
    core_.reset();
}

QVector3D MagQuality::patchCenter(int index, int patchCount)
{
    return Directions::patchCenter(index, patchCount);
}

int MagQuality::patch(const QVector3D &v, int patchCount)
{
    return Directions::patch(v, patchCount);
}

bool MagQuality::add(const QVector3D &mag, const QVector3D *accel,
                     float dipDegrees, float oneG)
{
    if (accel == nullptr) {
        return core_.add(toVec3f(mag), nullptr, dipDegrees, oneG);
    }
    const sensormath::Vec3f a = toVec3f(*accel);
    return core_.add(toVec3f(mag), &a, dipDegrees, oneG);
}

MagQuality::Result MagQuality::result() const
{
    return core_.result();
}
