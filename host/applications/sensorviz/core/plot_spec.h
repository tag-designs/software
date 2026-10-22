/**
 * @file    plot_spec.h
 * @brief   What sensorViz's plot should show, as plain data, and the pure function that decides it.
 *
 * @details Part of the Qt-free `sensorviz_core`. The plot is split in two.
 *          buildPlotSpec() holds the display policy: which traces are drawn on
 *          which axes, how each y axis gets its range, the time axis, event
 *          markers, the metadata box and the title. A renderer (PlotView,
 *          with QCustomPlot) turns a PlotSpec into drawing calls. A PlotSpec
 *          holds only standard types, so `plotspec_check` checks the policy
 *          against logs without Qt, and a different renderer, such as a
 *          webview using uPlot, could draw the same spec.
 *
 * @see     view_model.h, plot_view.h, host/applications/sensorviz/README.md
 */
#ifndef SENSORVIZ_CORE_PLOT_SPEC_H
#define SENSORVIZ_CORE_PLOT_SPEC_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "sensordata.h"
#include "stream_meta.h"

namespace sensorviz {

/**
 * @struct  PlotRange
 * @brief   A closed value or time interval.
 */
struct PlotRange
{
    double lower = 0.0; ///< Lower bound.
    double upper = 0.0; ///< Upper bound.

    /// Equal when both bounds are equal.
    bool operator==(const PlotRange &other) const
    {
        return lower == other.lower && upper == other.upper;
    }
};

/**
 * @struct  PlotState
 * @brief   The current view's choices that the plot depends on.
 *
 * @details Built by ViewModel::plotState() from the view state it owns.
 */
struct PlotState
{
    std::vector<std::string> visibleStreamIds;       ///< Streams to draw, in drawing and axis order.
    std::map<std::string, PlotRange> customRanges;   ///< Stream id to a y range the user set (or one linked to it, such as altitude from pressure).
    int utcOffsetHours = 0;                          ///< Offset applied to epoch-time tick labels.
    std::string title;                               ///< Graph title text.
    bool titleVisible = true;                        ///< Whether the title is shown; an empty title is never shown.
    double declinationDegrees = 0.0;                 ///< Shown in the metadata box while compass heading is visible.
    double seaLevelPressure = 1013.25;               ///< mbar; shown in the metadata box while altitude is visible.
    bool breakAtGaps = true;                         ///< Break plotted lines at gaps in the data (sensordata::GapMode::Break).
};

/**
 * @struct  PlotDisplayPreferences
 * @brief   The user's per-stream display overrides of the stream defaults.
 */
struct PlotDisplayPreferences
{
    std::map<std::string, Rgb> colors;         ///< Stream id to a color chosen by the user.
    std::map<std::string, AxisSide> axisSides; ///< Stream id to an axis side chosen by the user.
};

/**
 * @struct  PlotLogInfo
 * @brief   What the plot policy needs to know about the loaded log.
 */
struct PlotLogInfo
{
    sensordata::TimeDomain timeDomain = sensordata::TimeDomain::EpochSeconds; ///< The plot's time axis.
    bool hasCollectionStart = false;          ///< Whether #collectionStartEpochMs is known.
    std::int64_t collectionStartEpochMs = 0;  ///< Absolute start of collection, ms since the Unix epoch.
    std::vector<double> eventMarkerTimes;     ///< Event marker times in #timeDomain; markers in another domain are left out by the caller.
    const sensordata::Log *data = nullptr;    ///< The log's plot data, for ranges and time extents; may be null.
};

/**
 * @enum    PlotAxisRangeMode
 * @brief   How a y axis gets its range.
 */
enum class PlotAxisRangeMode
{
    /// The stream's own data range, enlarged to include the axis's current
    /// range and then padded by 5%: QCPGraph::rescaleValueAxis(true)
    /// semantics, which the renderer applies because they depend on the
    /// axis's history. PlotAxisSpec::lower and upper hold the data range.
    Auto,
    /// Shared by two or more visible streams of an axis group; lower and
    /// upper hold the padded union of their data ranges.
    Linked,
    /// Exactly lower..upper: a range the user set, or a stream's fixed
    /// default range such as activity 0-100.
    Fixed,
};

/**
 * @struct  PlotAxisSpec
 * @brief   One y axis, shared by the visible traces of one axis group.
 *
 * @details A stream with no group (StreamMeta::group) has an axis of its own.
 *          An axis with more than one trace is a shared axis: its members are
 *          told apart by their line colors, so the renderer shows a legend
 *          naming them.
 */
struct PlotAxisSpec
{
    AxisSide side = AxisSide::Left;  ///< Which side of the plot.
    std::string label;               ///< Axis title, UTF-8: stream label and units, or for a shared axis the group label and units.
    Rgb color;                       ///< Axis, tick and label color: that of its first trace.
    PlotAxisRangeMode mode = PlotAxisRangeMode::Auto; ///< See PlotAxisRangeMode.
    std::string linkedGroup;         ///< Group id when #mode is Linked, e.g. `imu_accel`.
    std::vector<int> traces;         ///< Indices into PlotSpec::traces drawn on this axis, in trace order; never empty.
    bool hasRange = false;           ///< False for an Auto axis whose stream has no finite data range; the renderer then pads the axis's current range.
    double lower = 0.0;              ///< See PlotAxisRangeMode.
    double upper = 0.0;              ///< See PlotAxisRangeMode.
};

/**
 * @struct  PlotTraceSpec
 * @brief   One drawn stream.
 */
struct PlotTraceSpec
{
    std::string id;    ///< Stream id.
    std::string name;  ///< Stream label, UTF-8.
    sensordata::StreamId stream = sensordata::kNoStream; ///< Plot data in the log's sensordata::Log; a trace without any is drawn empty.
    Rgb color;         ///< Line color.
    int penWidth = 1;  ///< Line width in pixels; derived streams are drawn heavier.
    int axis = 0;      ///< Index into PlotSpec::axes.
};

/**
 * @struct  PlotSpec
 * @brief   Everything the plot shows, independent of how it is drawn.
 */
struct PlotSpec
{
    sensordata::TimeDomain timeDomain = sensordata::TimeDomain::EpochSeconds; ///< Which time axis: elapsed seconds or epoch date/time.
    int utcOffsetHours = 0;                 ///< For epoch tick labels.
    bool hasXDataRange = false;             ///< False when no trace has samples.
    PlotRange xDataRange;                   ///< Earliest and latest sample time over all traces, unpadded; the full-range zoom.
    std::vector<PlotTraceSpec> traces;      ///< In drawing order.
    std::vector<PlotAxisSpec> axes;         ///< One per axis group with a visible trace, in order of each group's first trace.
    std::vector<double> eventMarkerTimes;   ///< Vertical marker lines, in the plot's time domain.
    std::vector<std::string> metadataRows;  ///< Lines of the metadata box; empty hides it.
    std::string title;                      ///< Title text, UTF-8; empty hides it.
    bool breakAtGaps = true;                ///< Lines break at gaps in the data rather than joining across them.
};

/**
 * @brief   Decides what the plot shows.
 *
 * @details Pure: reads only its arguments, and the log's sensordata::Log for
 *          stream ranges and time extents.
 *
 *          Visible streams with the same axisGroupKey() share one y axis; any
 *          other stream has its own. The rules, in order of precedence, for
 *          an axis's range:
 *
 *          1. the custom range from @p state of its first member that has
 *             one is Fixed;
 *          2. a lone member's default fixed range (StreamMeta::axisRange) is
 *             Fixed;
 *          3. two or more members are Linked, to the padded union of their
 *             data ranges, ignoring their default fixed ranges;
 *          4. otherwise Auto.
 *
 *          Colors are the stream defaults unless @p preferences override
 *          them. An axis's side is that of its first member with a side
 *          override in @p preferences, or else its first member's default.
 *          Derived streams are drawn two pixels wide.
 *
 *          The metadata box shows the collection start for elapsed-time logs
 *          that record one, the declination while `compass_heading` is
 *          visible, and the sea-level pressure while `altitude` is visible.
 *          Event markers are included while any trace is.
 *
 * @param[in] log          The loaded log.
 * @param[in] streams      Every stream currently available, raw and derived.
 * @param[in] preferences  The user's per-stream color and axis-side overrides.
 * @param[in] state        Visible streams, custom ranges and view settings.
 *                         Ids that name no stream are ignored.
 *
 * @return  The spec. With no visible streams it has no traces or axes and
 *          hasXDataRange is false.
 */
PlotSpec buildPlotSpec(const PlotLogInfo &log,
                       const std::vector<StreamMeta> &streams,
                       const PlotDisplayPreferences &preferences,
                       const PlotState &state);

/**
 * @brief   Pads a value range by 5% each side, giving a flat range a usable span.
 *
 * @details A range of zero or negative size becomes centred on its midpoint
 *          with a span of 10% of the midpoint's magnitude, or 1 if that is
 *          larger, before padding.
 *
 * @param[in] range  Range to pad.
 * @return  The padded range.
 */
PlotRange paddedRange(const PlotRange &range);

/**
 * @brief   The key under which a stream shares a y axis with others.
 *
 * @details Streams share an axis when they are in the same group and have
 *          the same units, so a record set whose columns mix units, such as
 *          CompassTag's accelerometer and magnetometer, gets one axis per
 *          unit.
 *
 * @param[in] stream  The stream.
 * @return  Its group and units, or an empty string when it has no group and
 *          so an axis of its own.
 */
std::string axisGroupKey(const StreamMeta &stream);

/**
 * @brief   Whole-stream finite value range from a log's plot data.
 * @param[in]  data    The log's plot data; may be null.
 * @param[in]  stream  Stream to measure.
 * @param[out] range   The range, when the result is true.
 * @return  false when there is no data, or its minimum or maximum is not
 *          finite (all NaN, or an infinite sample).
 */
bool plotDataRange(const sensordata::Log *data, sensordata::StreamId stream, PlotRange &range);

/**
 * @brief   Formats the collection start as the metadata box shows it.
 * @param[in] epochMs  Milliseconds since the Unix epoch.
 * @return  `MM/dd/yy hh:mm:ss UTC`, rounded to the nearest second.
 */
std::string formatCollectionStart(std::int64_t epochMs);

} // namespace sensorviz

#endif
