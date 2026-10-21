#ifndef SENSORVIZ_SENSOR_PREFERENCES_H
#define SENSORVIZ_SENSOR_PREFERENCES_H

#include <QColor>
#include <QMap>
#include <QSet>
#include <QString>

#include "sensorstream.h"
#include "view_model.h"

// Built-in display defaults per stream id are sensorviz::defaultDisplayForStream()
// (core/stream_meta.h). User overrides that follow a tag type are
// sensorviz::TagPreferences (core/view_model.h), applied to and captured from
// the ViewModel; this front end reads and writes them as JSON files:
//
// - visible_streams, only when visibility differs from the defaults;
// - colors and axis_sides, only for user choices;
// - break_lines_at_gaps, only when turned off.
//
// Session calculation parameters such as sea-level pressure, declination,
// battery-forward, UTC offset, and manual y-axis ranges are deliberately not
// written to preference files.
using SensorVizPreferences = sensorviz::TagPreferences;

#endif
