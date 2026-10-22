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

std::string axisGroupKey(const StreamMeta &stream)
{
    // Units are part of the key so that a group never puts two scales on one
    // axis. The separator cannot occur in a group id or units string.
    return stream.group.empty() ? std::string() : stream.group + '\x1f' + stream.units;
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

    // One axis per group of visible streams, in order of each group's first
    // member; a stream with no group gets an axis of its own.
    std::vector<std::vector<const StreamMeta *>> axis_members;
    std::map<std::string, size_t> axis_of_group;
    for (const StreamMeta *stream : visible) {
        const std::string key = axisGroupKey(*stream);
        const auto existing = key.empty() ? axis_of_group.end() : axis_of_group.find(key);
        if (existing != axis_of_group.end()) {
            axis_members[existing->second].push_back(stream);
            continue;
        }
        if (!key.empty()) {
            axis_of_group[key] = axis_members.size();
        }
        axis_members.push_back({stream});
    }

    std::map<const StreamMeta *, int> trace_axis;
    for (const std::vector<const StreamMeta *> &members : axis_members) {
        const StreamMeta *first = members.front();
        PlotAxisSpec axis;

        // The first member with a side override places the axis; otherwise
        // the first member's default does.
        axis.side = first->axisSide;
        for (const StreamMeta *member : members) {
            const auto side_override = preferences.axisSides.find(member->id);
            if (side_override != preferences.axisSides.end()) {
                axis.side = side_override->second;
                break;
            }
        }
        const auto color_override = preferences.colors.find(first->id);
        axis.color =
            color_override != preferences.colors.end() ? color_override->second : first->color;
        const std::string units = first->units.empty() ? std::string() : " " + first->units;
        axis.label = (members.size() > 1 && !first->groupLabel.empty() ? first->groupLabel
                                                                      : first->label)
            + units;

        const PlotRange *custom = nullptr;
        for (const StreamMeta *member : members) {
            const auto found = state.customRanges.find(member->id);
            if (found != state.customRanges.end()) {
                custom = &found->second;
                break;
            }
        }
        if (custom) {
            axis.mode = PlotAxisRangeMode::Fixed;
            axis.hasRange = true;
            axis.lower = custom->lower;
            axis.upper = custom->upper;
        } else if (members.size() == 1 && first->axisRange.enabled) {
            axis.mode = PlotAxisRangeMode::Fixed;
            axis.hasRange = true;
            axis.lower = first->axisRange.lower;
            axis.upper = first->axisRange.upper;
        } else if (members.size() > 1) {
            // Default fixed ranges describe one stream, such as |a| from 0;
            // they would clip the signed x/y/z members sharing the axis.
            PlotRange united;
            bool any = false;
            for (const StreamMeta *member : members) {
                PlotRange range;
                if (!streamDataRange(data, *member, range)) {
                    continue;
                }
                if (any) {
                    extend(united, range);
                } else {
                    united = range;
                    any = true;
                }
            }
            if (any) {
                const PlotRange padded = paddedRange(united);
                axis.mode = PlotAxisRangeMode::Linked;
                axis.linkedGroup = first->group;
                axis.hasRange = true;
                axis.lower = padded.lower;
                axis.upper = padded.upper;
            }
        } else {
            PlotRange range;
            axis.hasRange = streamDataRange(data, *first, range);
            axis.lower = range.lower;
            axis.upper = range.upper;
        }

        for (const StreamMeta *member : members) {
            trace_axis[member] = static_cast<int>(spec.axes.size());
        }
        spec.axes.push_back(axis);
    }

    bool heading_visible = false;
    bool altitude_visible = false;
    for (const StreamMeta *stream : visible) {
        heading_visible = heading_visible || stream->id == "compass_heading";
        altitude_visible = altitude_visible || stream->id == "altitude";

        const auto color_override = preferences.colors.find(stream->id);
        PlotTraceSpec trace;
        trace.id = stream->id;
        trace.name = stream->label;
        trace.stream = stream->dataId;
        trace.color =
            color_override != preferences.colors.end() ? color_override->second : stream->color;
        trace.penWidth = stream->derived ? 2 : 1;
        trace.axis = trace_axis[stream];
        spec.axes[static_cast<size_t>(trace.axis)].traces.push_back(
            static_cast<int>(spec.traces.size()));
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
