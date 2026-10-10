#include "posecheck.h"

#include "directions.h"

#include <algorithm>
#include <cmath>

PoseCheck::PoseCheck()
    : PoseCheck(Config())
{
}

PoseCheck::PoseCheck(const Config &config)
    : config_(config), holding_(-1), held_(0)
{
    if (config_.toleranceDegrees <= 0.0f) {
        config_.toleranceDegrees = 1.0f;
    } else if (config_.toleranceDegrees > 60.0f) {
        // Beyond this the six cones overlap and a reading could satisfy two
        // poses at once, which makes the checklist meaningless.
        config_.toleranceDegrees = 60.0f;
    }
    if (config_.holdSamples < 1) {
        config_.holdSamples = 1;
    }
    reset();
}

void PoseCheck::reset()
{
    done_.fill(false, PoseCount);
    holding_ = -1;
    held_ = 0;
}

QVector3D PoseCheck::direction(int pose)
{
    switch (pose) {
    case XDown: return QVector3D(-1.0f, 0.0f, 0.0f);
    case XUp:   return QVector3D(1.0f, 0.0f, 0.0f);
    case YDown: return QVector3D(0.0f, -1.0f, 0.0f);
    case YUp:   return QVector3D(0.0f, 1.0f, 0.0f);
    case ZDown: return QVector3D(0.0f, 0.0f, -1.0f);
    case ZUp:   return QVector3D(0.0f, 0.0f, 1.0f);
    default:    return QVector3D();
    }
}

bool PoseCheck::add(const QVector3D &accel, float oneG)
{
    if (!Directions::finite(accel) || !(oneG > 0.0f)) {
        return false;
    }

    // Being moved into position, not resting in it.
    const float magnitude = accel.length();
    if (magnitude <= 0.0f
        || std::fabs(magnitude - oneG) > config_.magnitudeGate * oneG) {
        holding_ = -1;
        held_ = 0;
        return false;
    }

    // An accelerometer at rest reads the specific force, which points
    // opposite to gravity: lay the tag with its z face down and it reads +z.
    // The pose is named for the face that is down, so the reading to match is
    // the outward normal of that face.
    const QVector3D unit = accel / magnitude;
    const float limit = std::cos(config_.toleranceDegrees
                                 * 3.14159265358979323846f / 180.0f);
    int nearest = -1;
    float best = -2.0f;
    for (int pose = 0; pose < PoseCount; pose++) {
        const float dot = QVector3D::dotProduct(unit, direction(pose));
        if (dot > best) {
            best = dot;
            nearest = pose;
        }
    }
    if (nearest < 0 || best < limit) {
        holding_ = -1;
        held_ = 0;
        return false;
    }

    // Counting consecutive readings in the same pose is what separates
    // holding it there from turning through it.
    if (nearest != holding_) {
        holding_ = nearest;
        held_ = 1;
    } else {
        held_++;
    }

    if (held_ >= config_.holdSamples && !done_.at(nearest)) {
        done_[nearest] = true;
        return true;
    }
    return false;
}

PoseCheck::Result PoseCheck::result() const
{
    Result r;
    r.done = done_;
    r.total = PoseCount;
    r.holding = holding_;
    r.heldSamples = held_;
    for (int pose = 0; pose < PoseCount; pose++) {
        if (done_.at(pose)) {
            r.completed++;
        }
    }
    r.finished = (r.completed == PoseCount);
    return r;
}
