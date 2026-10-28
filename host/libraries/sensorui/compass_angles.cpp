/**
 * @file    compass_angles.cpp
 * @brief   Display-only quaternion to heading, pitch and roll conversion.
 *
 * @details An adapter over compasscore::displayAngles() in sensoranalysis's
 *          Qt-free core, which holds the arithmetic so that a front end
 *          without Qt shows the same angles.
 */
#include "compass_angles.h"

#include "compass_types.h"

CompassAngles compassAnglesFromQuaternion(const QQuaternion &q)
{
    const compasscore::DisplayAngles angles = compasscore::displayAngles(toQuatf(q));
    CompassAngles result;
    result.yaw = angles.yaw;
    result.pitch = angles.pitch;
    result.roll = angles.roll;
    return result;
}
