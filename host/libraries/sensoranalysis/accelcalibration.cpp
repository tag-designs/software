/**
 * @file    accelcalibration.cpp
 * @brief   Qt adapter over calibcore::AccelCalibration.
 */
#include "accelcalibration.h"

#include "compass_types.h"

AccelCalibration::AccelCalibration()
    : core_()
{
}

AccelCalibration::AccelCalibration(const Config &config)
    : core_(config)
{
}

void AccelCalibration::reset()
{
    core_.reset();
}

bool AccelCalibration::add(const QVector3D &accel, float oneG)
{
    return core_.add(toVec3f(accel), oneG);
}

QVector<bool> AccelCalibration::occupancy() const
{
    const std::vector<bool> occupied = core_.occupancy();
    return QVector<bool>(occupied.begin(), occupied.end());
}

AccelCalibration::Result AccelCalibration::result(float oneG)
{
    const calibcore::AccelCalibration::Result c = core_.result(oneG);
    Result r;
    r.valid = c.valid;
    r.offset = toQVector3D(c.offset);
    r.radius = c.radius;
    r.residual = c.residual;
    r.samples = c.samples;
    r.patchesSeen = c.patchesSeen;
    r.patches = c.patches;
    r.coverage = c.coverage;
    r.isotropy = c.isotropy;
    r.offered = c.offered;
    r.gated = c.gated;
    return r;
}
