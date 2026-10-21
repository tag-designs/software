#ifndef SENSORVIZ_SENSORSTREAM_H
#define SENSORVIZ_SENSORSTREAM_H

#include <QColor>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

#include "compass_types.h"
#include "sensordata.h"
#include "stream_meta.h"

// sensorstream.h defines the normalized data model used everywhere after the
// SQLite loader finishes. The rest of sensorViz should work with these types
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

// Multi-column sensor data that is not itself a single plottable y-axis series.
// Compass data is the motivating future case: one Compass table has accel and
// magnetometer columns, and later transforms can turn those columns into scalar
// streams such as heading, pitch, roll, and acceleration magnitude.
struct SensorRecordSet
{
    // Stable id used by transform code, for example "compass_raw".
    QString id;

    // Human-readable label shown in File Info.
    QString label;

    // SQLite table holding the columns, all on one time axis. Seconds are in
    // the indicated domain.
    QString table;
    SensorTimeDomain timeDomain = SensorTimeDomain::EpochSeconds;

    // Column names, as the streams metadata gives them (value_column). Transform
    // code owns their interpretation and reads them as doubles with
    // sensordata::Log::forEachRow(table, columns, ...), which delivers rows in
    // time order with the time axis's own times.
    QStringList columns;

    // A stored stream on this table's time axis in SensorLog::data, so a
    // stream derived row by row from the record set can share that axis.
    sensordata::StreamId timeSource = sensordata::kNoStream;
};

// Discrete log events drawn as vertical plot markers. They are not value
// streams and do not own a y-axis.
struct SensorEventMarker
{
    QString id;
    QString label;
    QVector<double> time;
    SensorTimeDomain timeDomain = SensorTimeDomain::EpochSeconds;
};

// Loaded SQLite log plus the streams sensorViz knows how to display. Tag type
// is metadata for the File Info pane; UI features are driven by actual streams.
struct SensorLog
{
    // Original database path, used for File Info and default directory state.
    QString path;

    // Metadata read from the Info table. Feature availability is still driven
    // by actual streams/record sets, not this string.
    QString tagType;

    // Human-readable tag/log family name selected from Info.tagtype.
    QString profileName;

    // Raw info table values, with large opaque config/info blobs filtered out
    // only when the File Info tab is rendered.
    QMap<QString, QString> info;

    // Absolute start time of collection when the log provides it. Elapsed-time
    // logs use this as a human-readable anchor while plotting remains on the
    // elapsed axis; epoch-time logs usually leave it to the x-axis labels.
    bool hasCollectionStart = false;
    qint64 collectionStartEpochMs = 0;

    // Scalar series available to the View menu and plot.
    QVector<SensorStream> streams;

    // Multi-column tables available to transform code.
    QVector<SensorRecordSet> recordSets;

    // Event markers available to plot overlays.
    QVector<SensorEventMarker> eventMarkers;

    // CompassTag logs carry calibration constants in a separate table. Keep
    // them typed here rather than hiding them in the generic info map so the
    // next layer can derive heading/pitch/roll streams without reparsing JSON.
    bool hasCompassCalibration = false;
    qint64 compassCalibrationEpoch = 0;
    CompassCalibration compassCalibration;
    QString compassCalibrationWarning;

    SensorTimeDomain timeDomain = SensorTimeDomain::EpochSeconds;

    // The decimating plot backend for this log: float copies, min/max
    // pyramids and exact read-back. Shared because SensorLog is copied by
    // value between the loader thread and MainWindow; there is one Log per
    // loaded file.
    std::shared_ptr<sensordata::Log> data;
};

#endif
