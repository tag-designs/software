/**
 * @file    compass_angles.h
 * @brief   Display-only conversion of a compass orientation quaternion to
 *          heading, pitch and roll.
 *
 * @details CompassProcessor and the processed SQLite tables carry orientation
 *          as a quaternion only. Euler angles depend on a rotation order and an
 *          axis labelling that differ between libraries, so the conversion is
 *          done at the point of display, rather than in sensoranalysis or in
 *          stored data: here for C++ displays such as plotted streams, and in
 *          MyCompass.qml, which applies the same Qt conversion and relabelling
 *          to the quaternion it is given.
 *
 *          The convention is Qt's QQuaternion::toEulerAngles(), relabelled to
 *          the tag. Qt returns angles about x, y and z for the composition
 *          q = Ry * Rx * Rz; the x angle is shown as pitch, the y angle as
 *          roll, and the z angle, reversed so that it increases clockwise, as
 *          yaw. This is the convention every CompassTag display has used; it
 *          is not the aerospace Z-Y-X decomposition, and the two disagree when
 *          the tag is tilted about both horizontal axes.
 */
#ifndef COMPASS_ANGLES_H
#define COMPASS_ANGLES_H

#include <QQuaternion>

/**
 * @struct  CompassAngles
 * @brief   Heading, pitch and roll of one compass orientation, in degrees.
 *
 * @details All three are magnetic-frame values. Declination and the battery
 *          direction are display choices applied afterwards, by
 *          MyCompass.qml or by an application's heading stream.
 */
struct CompassAngles
{
    double yaw = 0.0;   ///< Magnetic yaw, degrees clockwise from magnetic north, [0, 360).
    double pitch = 0.0; ///< Rotation about the tag x axis, degrees, [-90, 90].
    double roll = 0.0;  ///< Rotation about the tag y axis, degrees, (-180, 180].
};

/**
 * @brief   Converts a CompassProcessor orientation quaternion to display angles.
 *
 * @details Uses the convention described in compass_angles.h. The input need
 *          not be normalized; Qt normalizes it. An identity or
 *          default-constructed quaternion yields all-zero angles.
 *
 * @param[in] q Orientation quaternion as returned in CompassDerivedSample::q
 *              or stored in the qw, qx, qy, qz columns of CompassOrientation.
 *
 * @return  Magnetic yaw, pitch and roll in degrees.
 *
 * @see     CompassProcessor::deriveSample(), host/commandline/design/dataprocessing.md
 */
CompassAngles compassAnglesFromQuaternion(const QQuaternion &q);

#endif // COMPASS_ANGLES_H
