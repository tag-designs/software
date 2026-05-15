#ifndef ATTITUDE_DISPLAY_H
#define ATTITUDE_DISPLAY_H

#include <QQuaternion>
#include <QQuickItem>

// Thin C++ facade for orientation_frame/MyAttitude.qml. Applications use this
// instead of calling QML method names directly when they want to display the
// tag's 3D attitude.
class AttitudeDisplay
{
public:
    void setRootObject(QQuickItem *rootObject);
    void setRotationQuaternion(const QQuaternion &rotation);
    void setBatteryForward(bool batteryForward);

private:
    QQuickItem *rootObject_ = nullptr;
};

#endif // ATTITUDE_DISPLAY_H
