/**
 * @file    plot_spec.h
 * @brief   What sensorViz's plot should show, as plain data, and the pure function that decides it.
 *
 * @details The plot is split in two. buildPlotSpec() holds the display
 *          policy: which traces are drawn on which axes, how each y axis gets
 *          its range, the time axis, event markers, the metadata box and the
 *          title. PlotView turns a PlotSpec into QCustomPlot calls and is the
 *          only code that touches QCustomPlot.
 *
 *          A PlotSpec has no QCustomPlot types, and buildPlotSpec() needs no
 *          window, so the policy can be checked against logs directly
 *          (`plotspec_check`). A different renderer, such as a webview using
 *          uPlot, could draw the same spec.
 *
 * @see     plot_view.h, host/applications/sensorviz/README.md
 */
#ifndef SENSORVIZ_PLOT_SPEC_H
#define SENSORVIZ_PLOT_SPEC_H

#include <QColor>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include "sensorstream.h"

/**
 * @struct  PlotRange
 * @brief   A closed value or time interval.
 */
struct PlotRange
{
    double lower = 0.0; ///< Lower bound.
    double upper = 0.0; ///< Upper bound.
};

/**
 * @struct  PlotState
 * @brief   The current view's choices that the plot depends on.
 *
 * @details Session state owned by MainWindow, collected each time the plot is
 *          rebuilt. None of it is saved with the display preferences.
 */
struct PlotState
{
    QStringList visibleStreamIds;           ///< Streams to draw, in drawing and axis order.
    QMap<QString, PlotRange> customRanges;  ///< Stream id to a y range the user set (or one linked to it, such as altitude from pressure).
    int utcOffsetHours = 0;                 ///< Offset applied to epoch-time tick labels.
    QString title;                          ///< Graph title text.
    bool titleVisible = true;               ///< Whether the title is shown; an empty title is never shown.
    double declinationDegrees = 0.0;        ///< Shown in the metadata box while compass heading is visible.
    double seaLevelPressure = 1013.25;      ///< mbar; shown in the metadata box while altitude is visible.
};

/**
 * @struct  PlotDisplayPreferences
 * @brief   The user's per-stream display overrides of the stream defaults.
 */
struct PlotDisplayPreferences
{
    QMap<QString, QColor> colors;            ///< Stream id to a color chosen by the user.
    QMap<QString, SensorAxisSide> axisSides; ///< Stream id to an axis side chosen by the user.
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
    /// Shared with the other visible streams of an x/y/z family; lower and
    /// upper hold the padded union of their data ranges.
    Linked,
    /// Exactly lower..upper: a range the user set, or a stream's fixed
    /// default range such as activity 0-100.
    Fixed,
};

/**
 * @struct  PlotAxisSpec
 * @brief   One y axis. Every trace has its own.
 */
struct PlotAxisSpec
{
    SensorAxisSide side = SensorAxisSide::Left; ///< Which side of the plot.
    QString label;                              ///< Axis title: stream label and units.
    QColor color;                               ///< Axis, tick and label color.
    PlotAxisRangeMode mode = PlotAxisRangeMode::Auto; ///< See PlotAxisRangeMode.
    QString linkedGroup;                        ///< Family id when #mode is Linked, e.g. `imu_accel`.
    bool hasRange = false;                      ///< False for an Auto axis whose stream has no finite data range; the renderer then pads the axis's current range.
    double lower = 0.0;                         ///< See PlotAxisRangeMode.
    double upper = 0.0;                         ///< See PlotAxisRangeMode.
};

/**
 * @struct  PlotTraceSpec
 * @brief   One drawn stream.
 */
struct PlotTraceSpec
{
    QString id;                    ///< Stream id.
    QString name;                  ///< Stream label.
    sensordata::StreamId stream = sensordata::kNoStream; ///< Plot data in the log's sensordata::Log; kNoStream when the stream has none.
    QVector<double> fallbackTime;  ///< The stream's own samples, used only when #stream is kNoStream.
    QVector<double> fallbackValue; ///< Values parallel to #fallbackTime.
    QColor color;                  ///< Line color.
    int penWidth = 1;              ///< Line width in pixels; derived streams are drawn heavier.
    int axis = 0;                  ///< Index into PlotSpec::axes.
};

/**
 * @struct  PlotSpec
 * @brief   Everything the plot shows, independent of how it is drawn.
 */
struct PlotSpec
{
    SensorTimeDomain timeDomain = SensorTimeDomain::EpochSeconds; ///< Which time axis: elapsed seconds or epoch date/time.
    int utcOffsetHours = 0;            ///< For epoch tick labels.
    bool hasXDataRange = false;        ///< False when no trace has samples.
    PlotRange xDataRange;              ///< Earliest and latest sample time over all traces, unpadded; the full-range zoom.
    QVector<PlotTraceSpec> traces;     ///< In drawing order.
    QVector<PlotAxisSpec> axes;        ///< One per trace, in trace order.
    QVector<double> eventMarkerTimes;  ///< Vertical marker lines, in the plot's time domain.
    QStringList metadataRows;          ///< Lines of the metadata box; empty hides it.
    QString title;                     ///< Title text; empty hides it.
};

/**
 * @brief   Decides what the plot shows.
 *
 * @details Pure: reads only its arguments, and the log's sensordata::Log for
 *          stream ranges and time extents. The rules, in order of
 *          precedence for each trace's y axis:
 *
 *          1. a custom range from @p state is Fixed;
 *          2. a stream's default fixed range (SensorStream::axisRange) is Fixed;
 *          3. an IMU x/y/z stream with at least one other visible member of its
 *             family that has neither of the above is Linked, to the padded
 *             union of the family's data ranges;
 *          4. otherwise Auto.
 *
 *          Colors and axis sides are the stream defaults unless @p preferences
 *          override them. Derived streams are drawn two pixels wide.
 *
 *          The metadata box shows the collection start for elapsed-time logs
 *          that record one, the declination while `compass_heading` is
 *          visible, and the sea-level pressure while `altitude` is visible.
 *          Event markers are included when their time domain is the log's.
 *
 * @param[in] log          The loaded log: time domain, collection start,
 *                         event markers and plot data.
 * @param[in] streams      Every stream currently available, raw and derived.
 * @param[in] preferences  The user's color and axis-side overrides.
 * @param[in] state        Visible streams, custom ranges and view settings.
 *                         Ids that name no stream are ignored.
 *
 * @return  The spec. With no visible streams it has no traces or axes and
 *          hasXDataRange is false.
 */
PlotSpec buildPlotSpec(const SensorLog &log,
                       const QVector<SensorStream> &streams,
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
 * @brief   The IMU x/y/z family a stream id belongs to, for linked y ranges.
 * @param[in] streamId  Stream id.
 * @return  `imu_accel`, `imu_gyro` or `imu_mag`, or an empty string.
 */
QString linkedAxisGroupId(const QString &streamId);

/**
 * @brief   Whole-stream finite value range from a log's plot data.
 * @param[in]  data    The log's plot data; may be null.
 * @param[in]  stream  Stream to measure.
 * @param[out] range   The range, when the result is true.
 * @return  false when there is no data, or its minimum or maximum is not
 *          finite (all NaN, or an infinite sample).
 */
bool plotDataRange(const sensordata::Log *data, sensordata::StreamId stream, PlotRange &range);

#endif
