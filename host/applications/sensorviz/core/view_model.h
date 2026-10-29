/**
 * @file    view_model.h
 * @brief   sensorViz's view state, owned in one Qt-free place.
 *
 * @details Part of `sensorviz_core`. The ViewModel holds everything the user
 *          chooses about the current view: which streams exist and which are
 *          shown, in what order, their custom ranges, colors and axis sides,
 *          the title, the UTC offset, the cursors, the gap mode and the
 *          transform parameters. Menus and dialogs are views of it: they read
 *          it to show check states and call its setters to change it. It also
 *          applies and captures the per-tag preferences, and builds the
 *          PlotState and PlotSpec the plot is drawn from.
 *
 *          It does not draw, load, compute transforms or read files. Setters
 *          return the Changes they made and also report them to the
 *          listener, so a front end can sync its widgets; when to redraw is
 *          left to the front end.
 *
 * @see     plot_spec.h, stream_meta.h, host/applications/sensorviz/README.md
 */
#ifndef SENSORVIZ_CORE_VIEW_MODEL_H
#define SENSORVIZ_CORE_VIEW_MODEL_H

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "plot_spec.h"
#include "stream_meta.h"

namespace sensorviz {

/**
 * @struct  TagPreferences
 * @brief   Display preferences that follow a tag type, stored as overrides of the defaults.
 *
 * @details Mirrors one tag entry of a sensorViz preference file; the JSON is
 *          read and written by the front end.
 */
struct TagPreferences
{
    std::string tagType;                       ///< Preference key, normally the log's tag type.
    bool hasVisibleStreamOverride = false;     ///< True when #visibleStreamIds is the complete set of visible streams, which may be empty.
    std::set<std::string> visibleStreamIds;    ///< Meaningful only with #hasVisibleStreamOverride.
    std::map<std::string, Rgb> streamColors;   ///< Colors the user chose.
    std::map<std::string, AxisSide> axisSides; ///< Axis sides the user chose.
    bool hasBreakLinesAtGapsOverride = false;  ///< True when the user turned off line breaks at gaps.
    bool breakLinesAtGaps = true;              ///< Meaningful only with #hasBreakLinesAtGapsOverride.

    /// True when any override is present; an entry without one is not stored.
    bool hasOverrides() const
    {
        return hasVisibleStreamOverride || !streamColors.empty() || !axisSides.empty()
            || hasBreakLinesAtGapsOverride;
    }
};

/**
 * @class   ViewModel
 * @brief   The view state behind sensorViz's menus, dialogs and plot.
 *
 * @details **Streams.** An ordered list of StreamMeta. Streams that are not
 *          StreamMeta::derived have a visibility of their own, set from
 *          StreamMeta::defaultVisible when added; derived streams (the
 *          activity filter) are visible while they exist. A stream added
 *          later is placed just after the last of its inputs, so the
 *          magnitudes follow their axes and altitude follows pressure.
 *
 *          **Changes.** Every setter returns the Changes it made, zero when
 *          the value was already set, and passes a non-zero result to the
 *          listener.
 *
 *          Not thread-safe; use from one thread.
 */
class ViewModel
{
public:
    /// What a call changed; combine with bitwise or.
    enum Change : unsigned {
        None = 0,
        Streams = 1u << 0,      ///< Streams were added, replaced or removed.
        Visibility = 1u << 1,   ///< Which streams are shown changed.
        Ranges = 1u << 2,       ///< Custom y ranges changed.
        Appearance = 1u << 3,   ///< Colors or axis sides changed.
        Title = 1u << 4,        ///< Title text or visibility changed.
        UtcOffset = 1u << 5,    ///< UTC offset changed.
        Cursors = 1u << 6,      ///< Cursor visibility changed.
        Gaps = 1u << 7,         ///< Gap mode changed.
        Parameters = 1u << 8,   ///< A transform parameter changed.
    };
    /// A set of Change flags.
    using Changes = unsigned;
    /// Called with the Changes of each call that changed something.
    using Listener = std::function<void(Changes)>;

    /// Default sea-level pressure, mbar.
    static constexpr double kDefaultSeaLevelPressure = 1013.25;
    /// Default activity filter time constant, seconds.
    static constexpr double kDefaultActivityTimeConstant = 600.0;

    /**
     * @brief   Sets the change listener, replacing any previous one.
     * @param[in] listener  Called after each change; may be empty.
     */
    void setListener(Listener listener);

    // -- Loading ------------------------------------------------------------

    /**
     * @brief   Starts the view of a newly loaded log.
     *
     * @details Replaces the streams, sets each one's visibility to its
     *          default, clears custom ranges, colors and sides, and shows
     *          @p title. The UTC offset, cursors, gap mode and transform
     *          parameters carry over, as they always have; preferences are
     *          applied separately.
     *
     * @param[in] streams  The log's stored streams, in metadata order.
     * @param[in] title    Graph title, normally the file name.
     * @return  Changes made.
     */
    Changes resetForLog(std::vector<StreamMeta> streams, const std::string &title);

    // -- Streams ------------------------------------------------------------

    /// All streams, in View-menu order.
    const std::vector<StreamMeta> &streams() const { return streams_; }

    /// The stream with @p id, or null; valid until the stream list changes.
    const StreamMeta *stream(const std::string &id) const;

    /// True when a stream with @p id exists.
    bool hasStream(const std::string &id) const { return stream(id) != nullptr; }

    /**
     * @struct  StreamUpdate
     * @brief   Result of addOrReplaceStream().
     */
    struct StreamUpdate
    {
        Changes changes = None;      ///< Changes made.
        bool replaced = false;       ///< True when a stream with the same id was replaced in place.
        sensordata::StreamId released = sensordata::kNoStream; ///< Plot data of the replaced stream that is no longer used, for the caller to free.
    };

    /**
     * @brief   Adds a stream, or replaces the one with the same id in place.
     * @param[in] stream   The stream.
     * @param[in] visible  Its visibility; ignored for a derived stream.
     * @return  What happened.
     */
    StreamUpdate addOrReplaceStream(const StreamMeta &stream, bool visible);

    /**
     * @brief   Removes a stream, with its custom range.
     * @param[in] id  Stream id.
     * @return  The stream's plot data, for the caller to free, or kNoStream
     *          when there was no such stream.
     */
    sensordata::StreamId removeStream(const std::string &id);

    // -- Visibility ---------------------------------------------------------

    /// Ids of the streams that have their own visibility (not derived), in order.
    std::vector<std::string> toggleableStreamIds() const;

    /// Whether @p id is shown; derived streams are shown while they exist.
    bool isVisible(const std::string &id) const;

    /// Shows or hides one non-derived stream. @return Changes made.
    Changes setStreamVisible(const std::string &id, bool visible);

    /**
     * @brief   Shows exactly the non-derived streams in @p ids.
     * @param[in] ids  Ids to show; unknown ids are ignored.
     * @return  Changes made.
     */
    Changes setVisibleStreams(const std::set<std::string> &ids);

    /// Shown streams in drawing order: non-derived ones in stream order, then derived ones.
    std::vector<std::string> visibleStreamIds() const;

    // -- Ranges -------------------------------------------------------------

    /// Custom y ranges by stream id.
    const std::map<std::string, PlotRange> &customRanges() const { return custom_ranges_; }

    /// True when @p id has a custom range.
    bool hasCustomRange(const std::string &id) const { return custom_ranges_.count(id) != 0; }

    /// True when the user set @p id's range directly, rather than through a linked stream.
    bool isExplicitRange(const std::string &id) const { return explicit_ranges_.count(id) != 0; }

    /**
     * @brief   Sets a custom y range.
     * @param[in] id        Stream id.
     * @param[in] range     The range.
     * @param[in] explicitly  True when the user set this stream's range
     *                      directly; false for a range linked from another
     *                      stream, such as altitude from pressure.
     * @return  Changes made.
     */
    Changes setCustomRange(const std::string &id, const PlotRange &range, bool explicitly);

    /// Clears every custom range and explicit flag (Reset Zoom). @return Changes made.
    Changes clearCustomRanges();

    // -- Colors and sides ---------------------------------------------------

    /// Color overrides by stream id.
    const std::map<std::string, Rgb> &colorOverrides() const { return colors_; }

    /// Replaces the color overrides. @return Changes made.
    Changes setColorOverrides(const std::map<std::string, Rgb> &colors);

    /// The color @p id is drawn in: its override, or its default.
    Rgb effectiveColor(const std::string &id) const;

    /// Axis-side overrides by stream id.
    const std::map<std::string, AxisSide> &axisSideOverrides() const { return sides_; }

    /// Replaces the axis-side overrides. @return Changes made.
    Changes setAxisSideOverrides(const std::map<std::string, AxisSide> &sides);

    /// The side @p id's axis is on: its override, or its default.
    AxisSide effectiveAxisSide(const std::string &id) const;

    // -- View settings ------------------------------------------------------

    const std::string &title() const { return title_; }         ///< Graph title.
    bool titleVisible() const { return title_visible_; }        ///< Whether the title is shown.
    Changes setTitle(const std::string &title);                 ///< @return Changes made.
    Changes setTitleVisible(bool visible);                      ///< @return Changes made.

    int utcOffsetHours() const { return utc_offset_hours_; }    ///< Offset for epoch tick labels.
    Changes setUtcOffsetHours(int hours);                       ///< @return Changes made.

    bool cursorsVisible() const { return cursors_visible_; }    ///< Whether the time cursors are shown.
    Changes setCursorsVisible(bool visible);                    ///< @return Changes made.

    bool breakAtGaps() const { return break_at_gaps_; }         ///< Whether lines break at gaps.
    Changes setBreakAtGaps(bool breakAtGaps);                   ///< @return Changes made.

    // -- Transform parameters -----------------------------------------------

    /// Parameter id of the sea-level pressure (altitude), mbar.
    static constexpr const char *kSeaLevelPressure = "altitude.sea_level_mbar";
    /// Parameter id of the declination added to heading (compass), degrees.
    static constexpr const char *kDeclination = "compass.declination_deg";
    /// Parameter id of the battery-forward heading convention (compass), 1 or 0.
    static constexpr const char *kBatteryForward = "compass.battery_forward";
    /// Parameter id of the activity filter's time constant, seconds.
    static constexpr const char *kActivityTimeConstant = "activity.time_constant_s";

    /**
     * @brief   A transform parameter by id, for front ends that render
     *          parameters as data (ParameterSpec).
     * @param[in] id  One of the k... parameter ids above.
     * @return  The value; a toggle as 1 or 0; NaN for an unknown id.
     */
    double parameter(const std::string &id) const;

    /**
     * @brief   Sets a transform parameter by id, through its typed setter.
     * @param[in] id     One of the k... parameter ids above.
     * @param[in] value  The value; a toggle is on when non-zero.
     * @return  Changes made; None for an unknown id or an unchanged value.
     */
    Changes setParameter(const std::string &id, double value);

    double seaLevelPressure() const { return sea_level_pressure_; }  ///< mbar, for altitude.
    Changes setSeaLevelPressure(double mbar);                        ///< @return Changes made.

    double declinationDegrees() const { return declination_degrees_; } ///< Added to heading.
    Changes setDeclinationDegrees(double degrees);                     ///< @return Changes made.

    bool batteryForward() const { return battery_forward_; }    ///< Heading convention: battery end forward.
    Changes setBatteryForward(bool forward);                    ///< @return Changes made.

    double activityTimeConstant() const { return activity_time_constant_; } ///< Activity filter, seconds.
    Changes setActivityTimeConstant(double seconds);                         ///< @return Changes made.

    // -- Preferences --------------------------------------------------------

    /**
     * @brief   The current choices as preferences for @p tagType.
     *
     * @details Visibility is stored when the set of shown non-derived streams
     *          differs from the default-visible set of all streams; colors
     *          and sides when they differ from the stream's default; the gap
     *          mode when it is off. This is the rule earlier sensorViz
     *          versions used, so files written by either are identical.
     *
     * @param[in] tagType  Preference key.
     * @return  The preferences; check TagPreferences::hasOverrides().
     */
    TagPreferences capturePreferences(const std::string &tagType) const;

    /**
     * @brief   Applies preferences, or the defaults where they have no override.
     *
     * @details Colors and sides for streams that do not exist are dropped.
     *
     * @param[in] preferences  Preferences for the current tag type; a
     *                         default-constructed value means "defaults".
     * @return  Changes made.
     */
    Changes applyPreferences(const TagPreferences &preferences);

    // -- Plot ---------------------------------------------------------------

    /// The state the plot is built from.
    PlotState plotState() const;

    /// The color and side overrides, as the plot policy takes them.
    PlotDisplayPreferences displayPreferences() const;

    /**
     * @brief   What the plot shows now.
     * @param[in] log  The loaded log.
     * @return  buildPlotSpec() of the current state.
     */
    PlotSpec plotSpec(const PlotLogInfo &log) const;

private:
    Changes notify(Changes changes);
    std::set<std::string> defaultVisibleIds() const;

    Listener listener_;
    std::vector<StreamMeta> streams_;
    std::set<std::string> visible_;             // Non-derived streams that are shown.
    std::map<std::string, PlotRange> custom_ranges_;
    std::set<std::string> explicit_ranges_;
    std::map<std::string, Rgb> colors_;
    std::map<std::string, AxisSide> sides_;
    std::string title_;
    bool title_visible_ = false;
    int utc_offset_hours_ = 0;
    bool cursors_visible_ = true;
    bool break_at_gaps_ = true;
    double sea_level_pressure_ = kDefaultSeaLevelPressure;
    double declination_degrees_ = 0.0;
    bool battery_forward_ = true;
    double activity_time_constant_ = kDefaultActivityTimeConstant;
};

} // namespace sensorviz

#endif
