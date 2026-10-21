/**
 * @file    plot_spec.cpp
 * @brief   sensorViz plot policy: buildPlotSpec() and its helpers.
 *
 * @details Everything here is a function of its arguments. The renderer,
 *          PlotView, applies the result; see plot_spec.h for the rules.
 */
#include "plot_spec.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace
{

/// Translates @p text in the PlotSpec context.
QString tr(const char *text)
{
    return QCoreApplication::translate("PlotSpec", text);
}

/// One metadata-box line: the label padded to a fixed width, then the value.
QString metadataRow(const QString &label, const QString &value)
{
    return QString("%1 %2").arg(label + ":", -8).arg(value);
}

/// The finite range of a stream's values, from its plot data.
bool streamDataRange(const sensordata::Log *data, const SensorStream &stream, PlotRange &range)
{
    return plotDataRange(data, stream.dataId, range);
}

/// Earliest and latest sample time of a stream, from its plot data.
bool streamTimeRange(const sensordata::Log *data, const SensorStream &stream, PlotRange &range)
{
    const sensordata::StreamInfo *info = data ? data->info(stream.dataId) : nullptr;
    if (!info) {
        return false;
    }
    range = PlotRange{info->first_time, info->last_time};
    return true;
}

/// Unites @p addition into @p range.
void extend(PlotRange &range, const PlotRange &addition)
{
    range.lower = std::min(range.lower, addition.lower);
    range.upper = std::max(range.upper, addition.upper);
}

} // namespace

QString linkedAxisGroupId(const QString &streamId)
{
    // Related physical axes should be compared on the same value scale. Keep
    // this narrowly scoped to true x/y/z families so unrelated streams with the
    // same units can still auto-scale independently.
    if (streamId == "imu_ax" || streamId == "imu_ay" || streamId == "imu_az") {
        return QStringLiteral("imu_accel");
    }
    if (streamId == "imu_gx" || streamId == "imu_gy" || streamId == "imu_gz") {
        return QStringLiteral("imu_gyro");
    }
    if (streamId == "imu_mx" || streamId == "imu_my" || streamId == "imu_mz") {
        return QStringLiteral("imu_mag");
    }
    return {};
}

PlotRange paddedRange(const PlotRange &range)
{
    // QCustomPlot's rescale can put max/min exactly on the frame. Add a small
    // margin so peaks remain visible and flat signals still get a useful span.
    double lower = range.lower;
    double upper = range.upper;
    double span = upper - lower;
    if (span <= 0.0) {
        const double center = (lower + upper) * 0.5;
        span = std::max(1.0, std::abs(center) * 0.1);
        lower = center - span * 0.5;
        upper = center + span * 0.5;
    }
    const double margin = span * 0.05;
    return PlotRange{lower - margin, upper + margin};
}

bool plotDataRange(const sensordata::Log *data, sensordata::StreamId stream, PlotRange &range)
{
    // Whole-stream value range from the plot backend's pyramid, ignoring NaN.
    const sensordata::StreamInfo *info = data ? data->info(stream) : nullptr;
    if (!info || !std::isfinite(info->min) || !std::isfinite(info->max)) {
        return false;
    }
    range = PlotRange{info->min, info->max};
    return true;
}

PlotSpec buildPlotSpec(const SensorLog &log,
                       const QVector<SensorStream> &streams,
                       const PlotDisplayPreferences &preferences,
                       const PlotState &state)
{
    PlotSpec spec;
    spec.timeDomain = log.timeDomain;
    spec.utcOffsetHours = state.utcOffsetHours;
    spec.title = state.titleVisible ? state.title : QString();
    const sensordata::Log *data = log.data.get();

    QVector<const SensorStream *> visible;
    for (const QString &id : state.visibleStreamIds) {
        for (const SensorStream &stream : streams) {
            if (stream.id == id) {
                visible.append(&stream);
                break;
            }
        }
    }

    // Linked families: count the visible members that would otherwise
    // auto-scale, and unite their data ranges.
    QMap<QString, int> group_counts;
    QMap<QString, PlotRange> group_ranges;
    for (const SensorStream *stream : visible) {
        const QString group = linkedAxisGroupId(stream->id);
        if (group.isEmpty() || state.customRanges.contains(stream->id)
            || stream->axisRange.enabled) {
            continue;
        }
        PlotRange range;
        if (!streamDataRange(data, *stream, range)) {
            continue;
        }
        group_counts[group]++;
        if (group_ranges.contains(group)) {
            extend(group_ranges[group], range);
        } else {
            group_ranges[group] = range;
        }
    }

    bool heading_visible = false;
    bool altitude_visible = false;
    for (const SensorStream *stream : visible) {
        heading_visible = heading_visible || stream->id == "compass_heading";
        altitude_visible = altitude_visible || stream->id == "altitude";

        const QColor color = preferences.colors.value(stream->id, stream->color);
        PlotAxisSpec axis;
        axis.side = preferences.axisSides.value(stream->id, stream->axisSide);
        axis.label = stream->label + (stream->units.isEmpty() ? QString() : " " + stream->units);
        axis.color = color;

        const QString group = linkedAxisGroupId(stream->id);
        if (state.customRanges.contains(stream->id)) {
            const PlotRange custom = state.customRanges.value(stream->id);
            axis.mode = PlotAxisRangeMode::Fixed;
            axis.hasRange = true;
            axis.lower = custom.lower;
            axis.upper = custom.upper;
        } else if (stream->axisRange.enabled) {
            axis.mode = PlotAxisRangeMode::Fixed;
            axis.hasRange = true;
            axis.lower = stream->axisRange.lower;
            axis.upper = stream->axisRange.upper;
        } else if (group_counts.value(group) > 1) {
            const PlotRange padded = paddedRange(group_ranges.value(group));
            axis.mode = PlotAxisRangeMode::Linked;
            axis.linkedGroup = group;
            axis.hasRange = true;
            axis.lower = padded.lower;
            axis.upper = padded.upper;
        } else {
            PlotRange range;
            axis.mode = PlotAxisRangeMode::Auto;
            axis.hasRange = streamDataRange(data, *stream, range);
            axis.lower = range.lower;
            axis.upper = range.upper;
        }

        PlotTraceSpec trace;
        trace.id = stream->id;
        trace.name = stream->label;
        trace.stream = stream->dataId;
        trace.color = color;
        trace.penWidth = stream->derived ? 2 : 1;
        trace.axis = static_cast<int>(spec.axes.size());
        spec.axes.append(axis);
        spec.traces.append(trace);

        PlotRange time_range;
        if (streamTimeRange(data, *stream, time_range)) {
            if (spec.hasXDataRange) {
                extend(spec.xDataRange, time_range);
            } else {
                spec.xDataRange = time_range;
                spec.hasXDataRange = true;
            }
        }
    }

    // Keep plot metadata compact and contextual. Epoch-time logs already have
    // date/time tick labels, so the absolute start time is shown only when
    // the plot itself uses the elapsed-time axis.
    if (log.timeDomain == SensorTimeDomain::ElapsedSeconds && log.hasCollectionStart) {
        const qint64 rounded_epoch_ms = ((log.collectionStartEpochMs + 500) / 1000) * 1000;
        const QDateTime start_time =
            QDateTime::fromMSecsSinceEpoch(rounded_epoch_ms, QTimeZone::UTC);
        spec.metadataRows.append(metadataRow(
            tr("Start"), start_time.toString("MM/dd/yy hh:mm:ss") + QStringLiteral(" UTC")));
    }
    if (heading_visible) {
        spec.metadataRows.append(
            metadataRow(tr("Decl"), tr("%1 deg").arg(state.declinationDegrees, 0, 'f', 2)));
    }
    if (altitude_visible) {
        spec.metadataRows.append(
            metadataRow(tr("MSL"), tr("%1 mbar").arg(state.seaLevelPressure, 0, 'f', 2)));
    }

    if (!visible.isEmpty()) {
        for (const SensorEventMarker &marker : log.eventMarkers) {
            if (marker.timeDomain == log.timeDomain) {
                spec.eventMarkerTimes += marker.time;
            }
        }
    }
    return spec;
}
