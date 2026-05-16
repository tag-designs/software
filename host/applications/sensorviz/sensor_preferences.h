#ifndef SENSORVIZ_SENSOR_PREFERENCES_H
#define SENSORVIZ_SENSOR_PREFERENCES_H

#include <QColor>
#include <QMap>
#include <QSet>
#include <QString>

#include "sensorstream.h"

// SensorVizPreferences contains viewer layout choices that should follow a tag
// type across log loads. These are deliberately separate from session
// parameters such as sea-level pressure and declination, which affect
// calculations for the current run but should not be written to preference
// files later.
struct SensorVizPreferences
{
    QString tagType;
    QSet<QString> visibleStreamIds;
    QMap<QString, QColor> streamColors;
    QMap<QString, SensorAxisSide> axisSides;
};

#endif
