#include "compass_processor.h"

CompassProcessor::CompassProcessor(const CompassCalibration &calibration)
    : calibration_(calibration)
{
}

CompassDerivedSample CompassProcessor::deriveSample(const CompassRawSample &sample) const
{
    CompassDerivedSample derived;
    deriveSample(sample, derived);
    return derived;
}

bool CompassProcessor::deriveSample(
    const CompassRawSample &sample,
    CompassDerivedSample &derived) const
{
    return deriveSample(sample, true, derived);
}

CompassDerivedSample CompassProcessor::deriveCalibratedSample(const CompassRawSample &sample) const
{
    CompassDerivedSample derived;
    deriveCalibratedSample(sample, derived);
    return derived;
}

bool CompassProcessor::deriveCalibratedSample(
    const CompassRawSample &sample,
    CompassDerivedSample &derived) const
{
    return deriveSample(sample, false, derived);
}

bool CompassProcessor::deriveSample(
    const CompassRawSample &sample,
    bool applyCalibration,
    CompassDerivedSample &derived) const
{
    derived = CompassDerivedSample();
    derived.epoch = sample.epoch;
    // The accelerometer offset is applied here rather than in
    // computeOrientation(), so the derived sample -- and the acceleration
    // magnitude drawn from it -- carries the corrected vector too.
    derived.accel = applyCalibration ? calibration_.applyAccel(sample.accel) : sample.accel;
    derived.mag = sample.mag;

    // The solve is the Qt-free core's; this class only converts types.
    const compasscore::CalibrationConstants constants = calibration_.constants();
    compasscore::Orientation orientation;
    if (!compasscore::solveOrientation(
            toVec3f(derived.mag),
            toVec3f(derived.accel),
            applyCalibration ? &constants : nullptr,
            orientation)) {
        return false;
    }

    // Orientation is the quaternion alone. Heading, pitch and roll are a
    // display convention; see compassAnglesFromQuaternion() in sensorui.
    derived.q = toQQuaternion(orientation.q);
    derived.dip = orientation.dip;
    derived.field = orientation.field;
    derived.mg = orientation.mg;
    return true;
}
