#include "compass_display.h"

#include <QMetaObject>
#include <QVariant>

void CompassDisplay::setRootObject(QQuickItem *rootObject)
{
    rootObject_ = rootObject;
}

void CompassDisplay::showSample(const CompassDerivedSample &sample)
{
    if (!rootObject_) {
        return;
    }

    // MyCompass.qml expects Euler angles plus diagnostic field values, not the
    // raw quaternion. Keep that presentation contract localized here.
    QMetaObject::invokeMethod(
        rootObject_,
        "setOrientation",
        Q_ARG(QVariant, sample.yaw),
        Q_ARG(QVariant, sample.pitch),
        Q_ARG(QVariant, sample.roll),
        Q_ARG(QVariant, sample.dip),
        Q_ARG(QVariant, sample.field),
        Q_ARG(QVariant, sample.mg));
}

void CompassDisplay::setDeclination(double declinationDegrees)
{
    if (!rootObject_) {
        return;
    }

    QMetaObject::invokeMethod(
        rootObject_, "setDeclination", Q_ARG(QVariant, declinationDegrees));
}

void CompassDisplay::setBatteryForward(bool batteryForward)
{
    if (!rootObject_) {
        return;
    }

    QMetaObject::invokeMethod(
        rootObject_, "setBatteryForward", Q_ARG(QVariant, batteryForward));
}
