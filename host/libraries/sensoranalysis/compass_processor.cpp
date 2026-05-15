#include "compass_processor.h"

#include <cmath>

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
    derived.accel = sample.accel;
    derived.mag = sample.mag;

    if (!computeOrientation(
            derived.mag,
            derived.accel,
            derived.q,
            derived.dip,
            derived.field,
            derived.mg,
            applyCalibration)) {
        return false;
    }

    QVector3D angles = derived.q.toEulerAngles();
    if (angles[2] < 0.0) {
        angles[2] += 360.0;
    }
    derived.pitch = angles[0];
    derived.roll = angles[1];
    derived.yaw = std::fmod(360 - angles[2], 360.0);

    return true;
}

double CompassProcessor::headingFromYaw(double yaw, double declinationDegrees)
{
    return std::fmod(720.0 + yaw + declinationDegrees, 360.0);
}

/*
 * Quaternion eCompass implementation based on:
 *
 *   Keeping a Good Attitude: A Quaternion-Based Orientation Filter for IMUs
 *   and MARGs
 *   Roberto G. Valenti, Ivan Dryanovski, Jizhong Xiao
 *
 * The algorithm assumes a north-west-up global frame. This function is kept
 * deliberately close to the original compviz implementation so that this
 * refactor changes ownership of the math, not the math itself.
 */
bool CompassProcessor::computeOrientation(
    QVector3D mag,
    QVector3D accel,
    QQuaternion &q,
    float &dip,
    float &field,
    float &mg,
    bool applyCalibration) const
{
    QQuaternion qacc;
    float q0, q1, q2, q3;

    if (applyCalibration) {
        mag = calibration_.apply(mag);
    }

    if ((mag.length() == 0.0) || (accel.length() == 0.0)) {
        return false;
    }

    field = mag.length();
    mg = accel.length();
    accel.normalize();
    mag.normalize();

    // Switch to the NWU convention used by the orientation algorithm.
    accel = QVector3D(accel[0], -accel[1], accel[2]);
    mag = QVector3D(mag[0], mag[1], -mag[2]);

    if (accel.z() >= 0) {
        q0 = sqrt((accel.z() + 1) * 0.5);
        q1 = -accel.y() / (2.0 * q0);
        q2 = accel.x() / (2.0 * q0);
        q3 = 0;
    } else {
        double x = sqrt((1 - accel.z()) * 0.5);
        q0 = -accel.y() / (2.0 * x);
        q1 = x;
        q2 = 0;
        q3 = accel.x() / (2.0 * x);
    }

    qacc = QQuaternion(q0, q2, q1, q3);
    qacc.normalize();

    QVector3D rotatedMag = qacc.rotatedVector(mag);

    float gamma = rotatedMag[0] * rotatedMag[0] + rotatedMag[1] * rotatedMag[1];
    float beta = sqrt(gamma + rotatedMag[0] * sqrt(gamma));
    float betaneg = sqrt(gamma - rotatedMag[0] * sqrt(gamma));
    float q0_mag, q3_mag;

    if (rotatedMag[0] >= 0.0) {
        q0_mag = beta / sqrt(2.0 * gamma);
        q3_mag = rotatedMag[1] / (sqrt(2.0) * beta);
    } else {
        q0_mag = rotatedMag[1] / (sqrt(2.0) * betaneg);
        q3_mag = betaneg / sqrt(2.0 * gamma);
    }

    QQuaternion qmag(q0_mag, 0, 0, -q3_mag);
    qmag = QQuaternion(sqrt(2) / 2, 0, 0, sqrt(2) / 2) * qmag;
    qmag.normalize();

    q = qacc * qmag;
    q.normalize();
    q.setX(-q.x());
    q.setY(-q.y());

    dip = atan2(rotatedMag[2], sqrt(gamma)) * 180.0 / M_PI;
    return true;
}
