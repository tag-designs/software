#include "attitude_display.h"

#include <QMetaObject>
#include <QVariant>

void AttitudeDisplay::setRootObject(QQuickItem *rootObject)
{
    rootObject_ = rootObject;
}

void AttitudeDisplay::setRotationQuaternion(const QQuaternion &rotation)
{
    if (!rootObject_) {
        return;
    }

    QMetaObject::invokeMethod(
        rootObject_, "setRotationQuaternion", Q_ARG(QVariant, rotation));
}

void AttitudeDisplay::setBatteryForward(bool batteryForward)
{
    if (!rootObject_) {
        return;
    }

    QMetaObject::invokeMethod(
        rootObject_, "setBatteryForward", Q_ARG(QVariant, batteryForward));
}
