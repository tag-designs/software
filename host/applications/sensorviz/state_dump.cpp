/**
 * @file    state_dump.cpp
 * @brief   sensorViz's maintainer-only state dump: a loaded log taken through
 *          a fixed script of setting changes, with everything the plot and
 *          readout would show written as text after each step.
 *
 * @details Implements MainWindow::writeStateDump(), behind the --dump-state
 *          option. It is the before-and-after check for moving the
 *          transforms into the core (toolkit-neutral core, phases D+E and F):
 *          the capture suites show only default settings, so this drives the
 *          changes they never make -- sea-level pressure, a pressure range
 *          and the altitude range linked to it, declination, battery
 *          forward, gaps, the UTC offset, Reset Zoom and, last, the activity
 *          filter -- through the same functions the dialogs and menu actions
 *          call, and records the result exactly:
 *
 *          - the PlotSpec, every field, numbers as hex floats;
 *          - every stream in the ViewModel: its metadata, visibility, and a
 *            hash of every sample as the plot backend holds it;
 *          - hover rows (time text and every stream's value text) at
 *            evenly spaced times;
 *          - the compass panel's orientation samples, hashed.
 *
 *          Two dumps of the same log are identical exactly when everything
 *          they cover is; compare them with diff. The format is for that, not
 *          for reading back.
 */

#include "mainwindow.h"

#include <QFile>
#include <QSignalBlocker>

#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace
{

/**
 * @class   Hash
 * @brief   FNV-1a, 64 bits, over the bytes of the values added.
 */
class Hash
{
public:
    /// Adds the bytes of @p value.
    template <typename T>
    void add(const T &value)
    {
        unsigned char bytes[sizeof(T)];
        std::memcpy(bytes, &value, sizeof(T));
        for (unsigned char byte : bytes) {
            state_ = (state_ ^ byte) * 0x100000001b3ULL;
        }
    }
    /// The hash as 16 hex digits.
    QString text() const { return QString::asprintf("%016" PRIx64, state_); }

private:
    std::uint64_t state_ = 0xcbf29ce484222325ULL; ///< FNV offset basis.
};

/// @p value exactly, as a C hex float.
QString hex(double value)
{
    // QString::asprintf() has no %a.
    char text[40];
    std::snprintf(text, sizeof text, "%a", value);
    return QString::fromLatin1(text);
}

/// A color as `#rrggbb`.
QString rgb(sensorviz::Rgb color)
{
    return QString::asprintf("#%02x%02x%02x", color.r, color.g, color.b);
}

/// A hash of the stream's M4 envelope at @p columns over its whole range.
QString envelopeHash(const sensordata::Log &log, sensordata::StreamId stream, int columns,
                     sensordata::GapMode gaps)
{
    const sensordata::Envelope envelope = log.view(stream, -INFINITY, INFINITY, columns, gaps);
    Hash hash;
    hash.add(envelope.time.size());
    for (size_t i = 0; i < envelope.time.size(); i++) {
        hash.add(envelope.time[i]);
        hash.add(envelope.value[i]);
        hash.add(envelope.index[i]);
    }
    return QString("%1:%2").arg(envelope.time.size()).arg(hash.text());
}

} // namespace

QString MainWindow::stateDumpStep(const QString &step) const
{
    QStringList lines;
    lines << "== " + step;

    const sensorviz::PlotSpec &spec = plot_view_->spec();
    lines << QString("spec domain=%1 utc=%2 x=%3 %4..%5 title=[%6] gaps=%7")
                 .arg(static_cast<int>(spec.timeDomain))
                 .arg(spec.utcOffsetHours)
                 .arg(spec.hasXDataRange)
                 .arg(hex(spec.xDataRange.lower), hex(spec.xDataRange.upper),
                      toQString(spec.title))
                 .arg(spec.breakAtGaps);
    for (const std::string &row : spec.metadataRows) {
        lines << "meta [" + toQString(row) + "]";
    }
    Hash markers;
    for (double time : spec.eventMarkerTimes) {
        markers.add(time);
    }
    lines << QString("markers %1 %2").arg(spec.eventMarkerTimes.size()).arg(markers.text());
    for (const sensorviz::PlotAxisSpec &axis : spec.axes) {
        QStringList traces;
        for (int trace : axis.traces) {
            traces << QString::number(trace);
        }
        lines << QString("axis side=%1 [%2] %3 mode=%4 group=%5 range=%6 %7..%8 traces=%9")
                     .arg(static_cast<int>(axis.side))
                     .arg(toQString(axis.label), rgb(axis.color))
                     .arg(static_cast<int>(axis.mode))
                     .arg(toQString(axis.linkedGroup))
                     .arg(axis.hasRange)
                     .arg(hex(axis.lower), hex(axis.upper), traces.join(','));
    }
    for (const sensorviz::PlotTraceSpec &trace : spec.traces) {
        lines << QString("trace %1 [%2] %3 pen=%4 axis=%5 data=%6")
                     .arg(toQString(trace.id), toQString(trace.name), rgb(trace.color))
                     .arg(trace.penWidth)
                     .arg(trace.axis)
                     .arg(log_.data && trace.stream != sensordata::kNoStream
                              ? envelopeHash(*log_.data, trace.stream, 1200,
                                             spec.breakAtGaps ? sensordata::GapMode::Break
                                                              : sensordata::GapMode::Join)
                              : QStringLiteral("none"));
    }

    for (const SensorStream &stream : view_.streams()) {
        const sensordata::StreamInfo *info = log_.data ? log_.data->info(stream.dataId) : nullptr;
        lines << QString("stream %1 [%2] [%3] group=%4 [%5] %6 derived=%7 default=%8 side=%9")
                     .arg(toQString(stream.id), toQString(stream.label), toQString(stream.units),
                          toQString(stream.group), toQString(stream.groupLabel),
                          rgb(stream.color))
                     .arg(stream.derived)
                     .arg(stream.defaultVisible)
                     .arg(static_cast<int>(stream.axisSide))
                 + QString(" range=%1 %2..%3 visible=%4 domain=%5")
                       .arg(stream.axisRange.enabled)
                       .arg(hex(stream.axisRange.lower), hex(stream.axisRange.upper))
                       .arg(isStreamVisible(toQString(stream.id)))
                       .arg(static_cast<int>(stream.timeDomain));
        if (info) {
            // One column per sample holds every sample of an evenly spaced stream.
            const int every = static_cast<int>(std::min<std::size_t>(info->samples, 4000000));
            lines << QString("  data n=%1 t=%2..%3 v=%4..%5 gaps=%6 all=%7 view=%8")
                         .arg(info->samples)
                         .arg(hex(info->first_time), hex(info->last_time),
                              hex(info->min), hex(info->max))
                         .arg(info->gaps)
                         .arg(envelopeHash(*log_.data, stream.dataId, every,
                                           sensordata::GapMode::Join),
                              envelopeHash(*log_.data, stream.dataId, 1200,
                                           sensordata::GapMode::Break));
        } else {
            lines << "  data none";
        }
    }

    for (const auto &[id, range] : view_.customRanges()) {
        lines << QString("range %1 %2..%3 explicit=%4")
                     .arg(toQString(id), hex(range.lower), hex(range.upper))
                     .arg(view_.isExplicitRange(id));
    }

    // Hover rows as showMousePosition() builds them, for every stream rather
    // than only the visible ones.
    if (spec.hasXDataRange) {
        const int points = 24;
        for (int k = 0; k <= points; k++) {
            const double time = spec.xDataRange.lower
                + (spec.xDataRange.upper - spec.xDataRange.lower) * k / points;
            QStringList row;
            row << formatPlotTime(time);
            for (const SensorStream &stream : view_.streams()) {
                row << streamValueAt(stream, time);
            }
            lines << "hover " + row.join(" | ");
        }
    }

    Hash compass;
    for (const CompassDerivedSample &sample : compass_samples_) {
        compass.add(sample.epoch);
        compass.add(sample.q.scalar());
        compass.add(sample.q.x());
        compass.add(sample.q.y());
        compass.add(sample.q.z());
        compass.add(sample.dip);
        compass.add(sample.field);
        compass.add(sample.mg);
    }
    lines << QString("compass %1 %2 [%3]")
                 .arg(compass_samples_.size())
                 .arg(compass.text(), compass_source_);
    return lines.join('\n') + '\n';
}

bool MainWindow::writeStateDump(const QString &path, QString &error)
{
    if (log_.path.empty()) {
        error = tr("No log is loaded.");
        return false;
    }

    QString dump = stateDumpStep(QStringLiteral("loaded"));

    applySeaLevelPressure(1000.0);
    dump += stateDumpStep(QStringLiteral("sea level 1000"));

    const QString pressure = hasStream("pressure") ? QStringLiteral("pressure")
                                                   : QStringLiteral("imu_pressure");
    if (hasStream(pressure)) {
        applyStreamRange(pressure, 950.0, 1010.0);
        dump += stateDumpStep(QStringLiteral("pressure range 950..1010"));
    }

    applyDeclination(12.5);
    dump += stateDumpStep(QStringLiteral("declination 12.5"));

    battery_forward_action_->setChecked(!battery_forward_action_->isChecked());
    dump += stateDumpStep(QStringLiteral("battery forward toggled"));

    break_gaps_action_->setChecked(false);
    dump += stateDumpStep(QStringLiteral("gaps joined"));

    if (log_.timeDomain == SensorTimeDomain::EpochSeconds) {
        applyUtcOffset(-5);
        dump += stateDumpStep(QStringLiteral("utc -5"));
    }

    resetZoom();
    dump += stateDumpStep(QStringLiteral("reset zoom"));

    // Last, because only the Qt app has the activity filter: a dump made
    // without Qt matches this one up to here.
    if (hasStream("activity")) {
        {
            // The action's own toggle would open the time-constant dialog.
            const QSignalBlocker blocker(activity_filter_action_);
            activity_filter_action_->setChecked(true);
        }
        enableActivityFilter(600.0);
        dump += stateDumpStep(QStringLiteral("activity filter 600"));
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = tr("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    file.write(dump.toUtf8());
    return file.error() == QFileDevice::NoError;
}
