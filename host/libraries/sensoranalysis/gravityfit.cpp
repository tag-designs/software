/**
 * @file    gravityfit.cpp
 * @brief   Qt adapter over calibcore::GravityFit.
 */
#include "gravityfit.h"

#include "compass_types.h"

GravityFit::GravityFit()
    : core_()
{
}

GravityFit::GravityFit(const Config &config)
    : core_(config)
{
}

void GravityFit::reset()
{
    core_.reset();
}

void GravityFit::refit(const QVector3D &offset)
{
    core_.refit(toVec3f(offset));
}

bool GravityFit::add(const QVector3D &accel, float oneG)
{
    return core_.add(toVec3f(accel), oneG);
}

GravityFit::Result GravityFit::result(float oneG) const
{
    const calibcore::GravityFit::Result c = core_.result(oneG);
    Result r;
    r.valid = c.valid;
    r.offset = toQVector3D(c.offset);
    r.radius = c.radius;
    r.residual = c.residual;
    r.samples = c.samples;
    return r;
}
