/**
 * @file    magretention.cpp
 * @brief   Qt adapter over calibcore::MagRetention.
 */
#include "magretention.h"

#include "compass_types.h"

#include <vector>

namespace {

/// The samples as sensormath vectors, in order.
std::vector<sensormath::Vec3f> toVec3fs(const QVector<QVector3D> &samples)
{
    std::vector<sensormath::Vec3f> out;
    out.reserve(samples.size());
    for (const QVector3D &v : samples) {
        out.push_back(toVec3f(v));
    }
    return out;
}

} // namespace

MagRetention::MagRetention()
    : core_()
{
}

MagRetention::MagRetention(const Config &config)
    : core_(config)
{
}

QVector<float> MagRetention::leverages(const QVector<QVector3D> &samples) const
{
    const std::vector<float> h = core_.leverages(toVec3fs(samples));
    return QVector<float>(h.begin(), h.end());
}

MagRetention::Choice MagRetention::choose(const QVector<QVector3D> &samples,
                                          const QVector<bool> &onProbation) const
{
    return core_.choose(toVec3fs(samples),
                        std::vector<bool>(onProbation.begin(), onProbation.end()));
}
