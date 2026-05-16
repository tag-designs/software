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

// Return the built-in display policy for a stable stream id. Unknown streams
// use conservative defaults: visible, left axis, neutral color, no fixed range.
// This is intentionally independent of tag type so stream ids remain the
// extension point; if a future tag emits "voltage", it should inherit the same
// voltage display policy unless there is a strong reason to introduce a new id.
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
    // Preference key, normally SensorLog::tagType. Stored here so JSON parsing
    // and diagnostics can carry the key with the override record.
    QString tagType;

    // False means use the default visibility from SensorStream::defaultVisible.
    // True means visibleStreamIds is the complete set of visible raw streams,
    // including the valid case where the user has hidden every stream.
    bool hasVisibleStreamOverride = false;
    QSet<QString> visibleStreamIds;

    // Sparse stream-id maps. Entries are present only when the user selected a
    // non-default color or axis side.
    QMap<QString, QColor> streamColors;
    QMap<QString, SensorAxisSide> axisSides;
};

#endif
