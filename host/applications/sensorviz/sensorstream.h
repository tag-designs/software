#ifndef SENSORVIZ_SENSORSTREAM_H
#define SENSORVIZ_SENSORSTREAM_H

#include <QColor>
#include <QString>

#include "compass_types.h"
#include "sensordata.h"
#include "stream_catalog.h"
#include "stream_meta.h"

// sensorstream.h defines the normalized data model used everywhere after
// sensorviz::openLog() reads a log. The rest of sensorViz should work with these types
// rather than with database table names. New stored streams are described by
// the SQLite streams metadata written by tagcore.

// The stream model is Qt-free and lives in sensorviz_core (core/stream_meta.h),
// so the view state and plot policy can be shared with a non-Qt front end.
// These aliases keep the names the rest of sensorViz uses.
//
// - SensorTimeDomain: seconds since the Unix epoch (most tags) or since the
//   start of collection (IMUTag, from SQLite ElapsedUs columns).
// - SensorAxisSide, SensorAxisRange: a stream's default axis side and fixed y
//   range.
// - SensorStream: one plottable scalar series, metadata only; its samples are
//   SensorStream::dataId in SensorLog::data. It is the main handoff type
//   between file loading and plotting. When adding a new sensor, prefer
//   converting its table into a new SensorStream id instead of adding
//   tag-specific checks to MainWindow.
using SensorTimeDomain = sensordata::TimeDomain;
using SensorAxisSide = sensorviz::AxisSide;
using SensorAxisRange = sensorviz::AxisRange;
using SensorStream = sensorviz::StreamMeta;

// Conversions between the core's standard types and Qt's, used only in the
// Qt front end. Core strings are UTF-8.
inline QString toQString(const std::string &text)
{
    return QString::fromStdString(text);
}
inline QColor toQColor(const sensorviz::Rgb &color)
{
    return QColor(color.r, color.g, color.b);
}
inline sensorviz::Rgb toRgb(const QColor &color)
{
    return sensorviz::Rgb{static_cast<std::uint8_t>(color.red()),
                          static_cast<std::uint8_t>(color.green()),
                          static_cast<std::uint8_t>(color.blue())};
}

// The loaded log's description is Qt-free too: core/stream_catalog.h, filled
// by sensorviz::openLog(). Its strings are UTF-8 std::string; convert with
// toQString() where Qt needs them.
//
// - SensorRecordSet: a multi-column table (compass_raw, *_orientation) that
//   transform code reads row by row.
// - SensorEventMarker: discrete events drawn as vertical plot markers.
// - SensorLog: everything known about the loaded log -- info rows, stored
//   streams, record sets, events, the compass calibration -- and its
//   sensordata::Log.
using SensorRecordSet = sensorviz::RecordSet;
using SensorEventMarker = sensorviz::EventMarker;
using SensorLog = sensorviz::StreamCatalog;

// The log's compass calibration as the Qt sensoranalysis type; exact.
inline CompassCalibration toCompassCalibration(const compasscore::CalibrationConstants &constants)
{
    return CompassCalibration::fromConstants(constants);
}

#endif
