/**
 * @file    directions.cpp
 * @brief   Qt adapter over calibcore::directions.
 */
#include "directions.h"

#include "compass_types.h"
#include "directions_core.h"

namespace Directions {

bool finite(const QVector3D &v)
{
    return sensormath::isFinite(toVec3f(v));
}

QVector3D patchCenter(int index, int patchCount)
{
    return toQVector3D(calibcore::directions::patchCenter(index, patchCount));
}

int patch(const QVector3D &v, int patchCount)
{
    return calibcore::directions::patch(toVec3f(v), patchCount);
}

void symmetricEigenvalues(const double a[3][3], double out[3])
{
    calibcore::directions::symmetricEigenvalues(a, out);
}

void accumulate(double moment[3][3], const QVector3D &v)
{
    calibcore::directions::accumulate(moment, toVec3f(v));
}

float isotropy(const double moment[3][3])
{
    return calibcore::directions::isotropy(moment);
}

} // namespace Directions
