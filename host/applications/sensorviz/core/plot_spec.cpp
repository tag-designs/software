/**
 * @file    plot_spec.cpp
 * @brief   sensorViz plot policy: buildPlotSpec() and its helpers.
 *
 * @details Everything here is a function of its arguments. The renderer,
 *          PlotView, applies the result; see plot_spec.h for the rules.
 */
#include "plot_spec.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>

namespace sensorviz {

namespace {

/// @p value with two decimals, independent of the process locale.
std::string fixed2(double value)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

/// One metadata-box line: the label and colon padded to eight characters, then the value.
std::string metadataRow(const std::string &label, const std::string &value)
{
    std::string head = label + ":";
    if (head.size() < 8) {
        head.append(8 - head.size(), ' ');
    }
    return head + " " + value;
}

/// The finite range of a stream's values, from its plot data.
bool streamDataRange(const sensordata::Log *data, const StreamMeta &stream, PlotRange &range)
{
    return plotDataRange(data, stream.dataId, range);
}

/// Earliest and latest sample time of a stream, from its plot data.
bool streamTimeRange(const sensordata::Log *data, const StreamMeta &stream, PlotRange &range)
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

std::string linkedAxisGroupId(const std::string &streamId)
{
    // Related physical axes should be compared on the same value scale. Keep
    // this narrowly scoped to true x/y/z families so unrelated streams with the
    // same units can still auto-scale independently.
    if (streamId == "imu_ax" || streamId == "imu_ay" || streamId == "imu_az") {
        return "imu_accel";
    }
    if (streamId == "imu_gx" || streamId == "imu_gy" || streamId == "imu_gz") {
        return "imu_gyro";
    }
    if (streamId == "imu_mx" || streamId == "imu_my" || streamId == "imu_mz") {
        return "imu_mag";
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

std::string formatCollectionStart(std::int64_t epochMs)
{
    const std::time_t seconds = static_cast<std::time_t>(((epochMs + 500) / 1000));
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    char text[32];
    std::strftime(text, sizeof text, "%m/%d/%y %H:%M:%S", &utc);
    return std::string(text) + " UTC";
}

PlotSpec buildPlotSpec(const PlotLogInfo &log,
                       const std::vector<StreamMeta> &streams,
                       const PlotDisplayPreferences &preferences,
                       const PlotState &state)
{
    PlotSpec spec;
    spec.timeDomain = log.timeDomain;
    spec.utcOffsetHours = state.utcOffsetHours;
    spec.title = state.titleVisible ? state.title : std::string();
    spec.breakAtGaps = state.breakAtGaps;
    const sensordata::Log *data = log.data;

    std::vector<const StreamMeta *> visible;
    for (const std::string &id : state.visibleStreamIds) {
        for (const StreamMeta &stream : streams) {
            if (stream.id == id) {
                visible.push_back(&stream);
                break;
            }
        }
    }

    // Linked families: count the visible members that would otherwise
    // auto-scale, and unite their data ranges.
    std::map<std::string, int> group_counts;
    std::map<std::string, PlotRange> group_ranges;
    for (const StreamMeta *stream : visible) {
        const std::string group = linkedAxisGroupId(stream->id);
        if (group.empty() || state.customRanges.count(stream->id) != 0
            || stream->axisRange.enabled) {
            continue;
        }
        PlotRange range;
        if (!streamDataRange(data, *stream, range)) {
            continue;
        }
        group_counts[group]++;
        const auto existing = group_ranges.find(group);
        if (existing != group_ranges.end()) {
            extend(existing->second, range);
        } else {
            group_ranges[group] = range;
        }
    }

    bool heading_visible = false;
    bool altitude_visible = false;
    for (const StreamMeta *stream : visible) {
        heading_visible = heading_visible || stream->id == "compass_heading";
        altitude_visible = altitude_visible || stream->id == "altitude";

        const auto color_override = preferences.colors.find(stream->id);
        const Rgb color =
            color_override != preferences.colors.end() ? color_override->second : stream->color;
        const auto side_override = preferences.axisSides.find(stream->id);
        PlotAxisSpec axis;
        axis.side = side_override != preferences.axisSides.end() ? side_override->second
                                                                 : stream->axisSide;
        axis.label = stream->label + (stream->units.empty() ? std::string() : " " + stream->units);
        axis.color = color;

        const std::string group = linkedAxisGroupId(stream->id);
        const auto custom = state.customRanges.find(stream->id);
        const auto group_count = group_counts.find(group);
        if (custom != state.customRanges.end()) {
            axis.mode = PlotAxisRangeMode::Fixed;
            axis.hasRange = true;
            axis.lower = custom->second.lower;
            axis.upper = custom->second.upper;
        } else if (stream->axisRange.enabled) {
            axis.mode = PlotAxisRangeMode::Fixed;
            axis.hasRange = true;
            axis.lower = stream->axisRange.lower;
            axis.upper = stream->axisRange.upper;
        } else if (!group.empty() && group_count != group_counts.end() && group_count->second > 1) {
            const PlotRange padded = paddedRange(group_ranges[group]);
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
        spec.axes.push_back(axis);
        spec.traces.push_back(trace);

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
    if (log.timeDomain == sensordata::TimeDomain::ElapsedSeconds && log.hasCollectionStart) {
        spec.metadataRows.push_back(
            metadataRow("Start", formatCollectionStart(log.collectionStartEpochMs)));
    }
    if (heading_visible) {
        spec.metadataRows.push_back(metadataRow("Decl", fixed2(state.declinationDegrees) + " deg"));
    }
    if (altitude_visible) {
        spec.metadataRows.push_back(metadataRow("MSL", fixed2(state.seaLevelPressure) + " mbar"));
    }

    if (!visible.empty()) {
        spec.eventMarkerTimes = log.eventMarkerTimes;
    }
    return spec;
}

} // namespace sensorviz
