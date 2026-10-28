#include "compassdata.h"

#include "compass_types.h"

CompassData::CompassData(QObject *parent) : QObject{parent}
{
    clear();
}

bool CompassData::addData(QVector3D &mag, bool hasAccel,
                          const QVector3D &accel)
{
    // When the solver accepts a fit, emit a UI update; when a calibration
    // exists, return the calibrated sample for plotting.
    if (session.addSample(toVec3f(mag), hasAccel, toVec3f(accel))) {
        emit calibration_update();
    }
    if (session.hasCalibration()) {
        mag = toQVector3D(session.calibrate(toVec3f(mag)));
        return true;
    }
    return false;
}

void CompassData::getData(QList<QVector3D> &data)
{
    for (const sensormath::Vec3f &point : session.calibratedPoints()) {
        data.append(toQVector3D(point));
    }
}

bool CompassData::getCalibrationConstants(float *B, float *V, float (*A)[3])
{
    return session.constants(B, V, A);
}

void CompassData::setCalibrationConstants(float *V, float (*A)[3])
{
    session.setConstants(V, A);
}

void CompassData::setAccelOffset(const QVector3D &offset)
{
    session.setAccelOffset(toVec3f(offset));
    refreshGravity();
}

float CompassData::getField()
{
    return session.field();
}

bool CompassData::eCompass(QVector3D magin, QVector3D accel, QQuaternion &q,
                           float& dip, float& field)
{
    compasscore::Orientation orientation;
    if (!session.orientation(toVec3f(magin), toVec3f(accel), orientation)) {
        return false;
    }
    q = toQQuaternion(orientation.q);
    dip = orientation.dip;
    field = orientation.field;
    return true;
}

void CompassData::clear()
{
    session.clear();
    refreshGravity();
}

void CompassData::qualityUpdate()
{
    session.updateQuality();
    refreshGravity();
}

void CompassData::refreshGravity()
{
    const calibcore::AccelCalibration::Result &c = session.accelOffset();
    gravity = AccelCalibration::Result();
    gravity.valid = c.valid;
    gravity.offset = toQVector3D(c.offset);
    gravity.radius = c.radius;
    gravity.residual = c.residual;
    gravity.samples = c.samples;
    gravity.patchesSeen = c.patchesSeen;
    gravity.patches = c.patches;
    gravity.coverage = c.coverage;
    gravity.isotropy = c.isotropy;
    gravity.offered = c.offered;
    gravity.gated = c.gated;
}
