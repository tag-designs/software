/**
 * @file posecheck_check.cc
 * @brief Offline assertion checks for PoseCheck.
 */

#include "posecheck.h"

#include <QQuaternion>

#include <cmath>
#include <cstdio>
#include <random>

namespace {

const float kOneG = 1000.0f;
int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("  %-60s %s\n", what, condition ? "ok" : "FAILED");
    if (!condition) {
        failures++;
    }
}

/// A reading from holding the tag with `pose`'s face down, tilted by `off`
/// degrees about an arbitrary axis, with a little noise.
QVector3D held(int pose, float offDegrees, std::mt19937 &rng)
{
    QVector3D u = PoseCheck::direction(pose);
    QVector3D axis = QVector3D::crossProduct(u, QVector3D(0.3f, 0.5f, 0.8f));
    if (axis.length() < 1e-3f) {
        axis = QVector3D::crossProduct(u, QVector3D(1.0f, 0.0f, 0.0f));
    }
    const QQuaternion tilt =
        QQuaternion::fromAxisAndAngle(axis.normalized(), offDegrees);
    std::normal_distribution<float> gauss(0.0f, 3.0f);
    return tilt.rotatedVector(u) * kOneG
           + QVector3D(gauss(rng), gauss(rng), gauss(rng));
}

void completesEveryPose()
{
    std::printf("completes every pose when each is held\n");
    std::mt19937 rng(1);
    PoseCheck poses;
    int announced = 0;
    for (int pose = 0; pose < PoseCheck::PoseCount; pose++) {
        for (int i = 0; i < 25; i++) {
            if (poses.add(held(pose, 5.0f, rng), kOneG)) {
                announced++;
            }
        }
    }
    const PoseCheck::Result r = poses.result();
    check(r.finished, "all six poses are done");
    check(r.completed == r.total, "and the count agrees");
    check(announced == PoseCheck::PoseCount,
          "each completion is announced exactly once");
}

void needsTheTagHeldStill()
{
    std::printf("needs the pose held, not passed through\n");
    std::mt19937 rng(2);
    PoseCheck poses;
    // One qualifying reading per pose, round and round: never the required
    // run in any one of them, which is what turning the tag over looks like.
    for (int sweep = 0; sweep < 30; sweep++) {
        for (int pose = 0; pose < PoseCheck::PoseCount; pose++) {
            poses.add(held(pose, 5.0f, rng), kOneG);
        }
    }
    check(poses.result().completed == 0, "nothing completes while it moves");

    for (int i = 0; i < 25; i++) {
        poses.add(held(PoseCheck::MinusZ, 5.0f, rng), kOneG);
    }
    check(poses.result().completed == 1, "holding one of them completes it");
    check(poses.result().done.at(PoseCheck::MinusZ), "and it is the right one");
}

void rejectsMotionAndSloppyPoses()
{
    std::printf("rejects motion and poses outside the tolerance\n");
    std::mt19937 rng(3);
    PoseCheck poses;

    for (int i = 0; i < 40; i++) {
        poses.add(PoseCheck::direction(PoseCheck::MinusZ) * (kOneG * 1.4f), kOneG);
    }
    check(poses.result().completed == 0, "a reading at 1.4 g never counts");

    for (int i = 0; i < 40; i++) {
        poses.add(held(PoseCheck::MinusZ, 45.0f, rng), kOneG);
    }
    check(poses.result().completed == 0, "nor one 45 degrees off the axis");

    for (int i = 0; i < 25; i++) {
        poses.add(held(PoseCheck::MinusZ, 18.0f, rng), kOneG);
    }
    check(poses.result().completed == 1,
          "18 degrees off is inside the 20 degree tolerance and counts");
}

void reportsWhatIsBeingHeld()
{
    std::printf("reports the pose in progress\n");
    std::mt19937 rng(4);
    PoseCheck poses;
    for (int i = 0; i < 5; i++) {
        poses.add(held(PoseCheck::PlusY, 5.0f, rng), kOneG);
    }
    PoseCheck::Result r = poses.result();
    check(r.holding == PoseCheck::PlusY, "the pose being held is named");
    check(r.heldSamples == 5, "with how long it has been held");
    check(!r.finished && r.completed == 0, "and it is not done yet");

    poses.add(QVector3D(0.0f, 0.0f, 0.0f), kOneG);
    check(poses.result().holding == -1, "a bad reading ends the hold");
}

void directionsAreOpposedPairs()
{
    std::printf("the six directions are three opposed pairs\n");
    bool paired = true;
    for (int pose = 0; pose < PoseCheck::PoseCount; pose += 2) {
        const QVector3D a = PoseCheck::direction(pose);
        const QVector3D b = PoseCheck::direction(pose + 1);
        paired = paired && (a + b).length() < 1e-6f;
        paired = paired && std::fabs(a.length() - 1.0f) < 1e-6f;
    }
    // This is what the whole choice of six rests on: for an opposed pair the
    // midpoint of the two readings is the offset exactly, whatever the
    // sensitivity, so three pairs give three independent estimates of it.
    check(paired, "each even pose is the negation of the odd one after it");
}

} // namespace

int main()
{
    std::printf("PoseCheck checks\n\n");
    completesEveryPose();
    needsTheTagHeldStill();
    rejectsMotionAndSloppyPoses();
    reportsWhatIsBeingHeld();
    directionsAreOpposedPairs();
    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "all ok", failures);
    return failures ? 1 : 0;
}
