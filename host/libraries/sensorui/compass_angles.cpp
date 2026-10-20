/**
 * @file    compass_angles.cpp
 * @brief   Display-only quaternion to heading, pitch and roll conversion.
 */
#include "compass_angles.h"

#include <QVector3D>

#include <cmath>

CompassAngles compassAnglesFromQuaternion(const QQuaternion &q)
{
    // Qt's angles are (about x, about y, about z) for q = Ry * Rx * Rz. The z
    // angle is counter-clockwise in the NWU solve; a compass reads clockwise.
    QVector3D angles = q.toEulerAngles();
    if (angles[2] < 0.0) {
        angles[2] += 360.0;
    }

    CompassAngles result;
    result.pitch = angles[0];
    result.roll = angles[1];
    result.yaw = std::fmod(360 - angles[2], 360.0);
    return result;
}
