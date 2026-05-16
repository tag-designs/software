#ifndef SENSORVIZ_SENSOR_PREFERENCES_H
#define SENSORVIZ_SENSOR_PREFERENCES_H

#include <QColor>
#include <QMap>
#include <QSet>
#include <QString>

#include "sensorstream.h"

// Default display policy for a stream id. SQLite logs provide scientific/log
// metadata such as id, label, units, and table columns; sensorViz owns these UI
// defaults because colors, initial visibility, axis side, and fixed display
// ranges are viewer choices.
struct SensorStreamDisplayDefaults
{
    QColor color = QColor(80, 100, 140);
    bool visible = true;
    SensorAxisSide axisSide = SensorAxisSide::Left;
    SensorAxisRange axisRange;
};

SensorStreamDisplayDefaults defaultDisplayForStream(const QString &stream_id);

// SensorVizPreferences contains viewer layout choices that should follow a tag
// type across log loads and may be saved to a JSON preference file. It stores
// only overrides from sensorViz defaults:
//
// - visibleStreamIds is meaningful only when hasVisibleStreamOverride is true.
// - streamColors contains only colors chosen by the user.
// - axisSides contains only left/right choices that differ from stream metadata.
//
// Session calculation parameters such as sea-level pressure, declination,
// battery-forward, UTC offset, and manual y-axis ranges stay on MainWindow and
// are deliberately not written to preference files.
struct SensorVizPreferences
{
    QString tagType;
    bool hasVisibleStreamOverride = false;
    QSet<QString> visibleStreamIds;
    QMap<QString, QColor> streamColors;
    QMap<QString, SensorAxisSide> axisSides;
};

#endif
