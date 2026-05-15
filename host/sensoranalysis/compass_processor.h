#ifndef COMPASS_PROCESSOR_H
#define COMPASS_PROCESSOR_H

#include "compass_types.h"

class CompassProcessor
{
public:
    explicit CompassProcessor(const CompassCalibration &calibration);

    // Convert one raw database row into magnetic-frame orientation data. This
    // deliberately does not apply declination or battery direction; those are
    // display settings handled by MainWindow::updateHeadingGraph().
    CompassDerivedSample deriveSample(const CompassRawSample &sample) const;

    // Shared helper for UI code that needs to turn magnetic yaw into displayed
    // heading using a user-selected angular offset.
    static double headingFromYaw(double yaw, double declinationDegrees);

private:
    bool computeOrientation(
        QVector3D mag,
        QVector3D accel,
        QQuaternion &q,
        float &dip,
        float &field,
        float &mg) const;

    CompassCalibration calibration_;
};

#endif // COMPASS_PROCESSOR_H
