#ifndef COMPASS_PROCESSOR_H
#define COMPASS_PROCESSOR_H

#include "compass_types.h"

class CompassProcessor
{
public:
    explicit CompassProcessor(const CompassCalibration &calibration);

    // Convert one raw sample into magnetic-frame orientation data. This
    // deliberately does not apply declination or battery direction; those are
    // display settings handled by application UI code.
    CompassDerivedSample deriveSample(const CompassRawSample &sample) const;
    bool deriveSample(const CompassRawSample &sample, CompassDerivedSample &derived) const;

    // qtcalibrate filters magnetometer samples after calibration. This variant
    // shares the same eCompass solve while accepting an already-calibrated
    // magnetometer vector.
    CompassDerivedSample deriveCalibratedSample(const CompassRawSample &sample) const;
    bool deriveCalibratedSample(
        const CompassRawSample &sample,
        CompassDerivedSample &derived) const;

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
        float &mg,
        bool applyCalibration) const;

    bool deriveSample(
        const CompassRawSample &sample,
        bool applyCalibration,
        CompassDerivedSample &derived) const;

    CompassCalibration calibration_;
};

#endif // COMPASS_PROCESSOR_H
