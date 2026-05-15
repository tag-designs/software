#ifndef COMPASS_DISPLAY_H
#define COMPASS_DISPLAY_H

#include "compass_types.h"

#include <QQuickItem>

// Thin C++ facade for orientation_frame/MyCompass.qml. Keeping this adapter
// small avoids leaking QML method names into plotting and menu code.
class CompassDisplay
{
public:
    void setRootObject(QQuickItem *rootObject);
    void showSample(const CompassDerivedSample &sample);
    void setDeclination(double declinationDegrees);
    void setBatteryForward(bool batteryForward);

private:
    QQuickItem *rootObject_ = nullptr;
};

#endif // COMPASS_DISPLAY_H
