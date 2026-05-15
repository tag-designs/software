#ifndef COMPASS_PROCESSOR_H
#define COMPASS_PROCESSOR_H

#include "compass_types.h"

class CompassProcessor
{
public:
    explicit CompassProcessor(const CompassCalibration &calibration);

    CompassDerivedSample deriveSample(const CompassRawSample &sample) const;

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
