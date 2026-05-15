#ifndef COMPASS_DISPLAY_H
#define COMPASS_DISPLAY_H

#include "compass_types.h"

#include <QQuickItem>

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
