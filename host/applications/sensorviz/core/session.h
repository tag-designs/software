/**
 * @file    session.h
 * @brief   Session: one opened log in sensorViz, the facade front ends use.
 *
 * @details Part of `sensorviz_core`. A Session owns the log's StreamCatalog,
 *          the ViewModel and the TransformRegistry, and holds the rules and
 *          text that sit between them and a screen: the hover readout, time
 *          formatting, the default and linked y ranges, Reset Zoom, and the
 *          preference key. Preferences outlive any one log, so the
 *          PreferenceStore stays with the front end and is passed in.
 *
 *          Two ways in:
 *
 *          - open() does a whole load, as File > Load does: read the log,
 *            reset the view, run the transforms, apply the tag's preferences.
 *            Front ends without Qt (`session_cli`, the Rust app) use it.
 *          - The Qt app loads on a worker thread and then goes through the
 *            same steps itself, redrawing between them as it always has, so
 *            it uses adopt(), runTransforms() and the setters one by one.
 *
 *          Text is UTF-8 and in English; the formats are fixed, and match
 *          what the Qt app has always shown (checked against Qt's own
 *          formatting).
 *
 *          Not thread-safe; use from one thread. Only open() may run on a
 *          worker thread, on a Session no other thread is using.
 *
 * @see     host/applications/sensorviz/design/proposals/toolkit-neutral-core.md
 */
#ifndef SENSORVIZ_CORE_SESSION_H
#define SENSORVIZ_CORE_SESSION_H

#include <string>
#include <utility>
#include <vector>

#include "plot_spec.h"
#include "preference_store.h"
#include "stream_catalog.h"
#include "transforms.h"
#include "view_model.h"

namespace sensorviz {

/**
 * @brief   A time as the hover readout shows it.
 *
 * @details Elapsed time as `Elapsed M:SS.sss s`, or `Elapsed H:MM:SS.sss s`
 *          from an hour, with a leading `-` before the start; epoch time as
 *          `MM/dd hh:mm:ss` at UTC plus @p utcOffsetHours, the seconds
 *          truncated toward zero, as `QDateTime::fromSecsSinceEpoch()` takes
 *          them.
 *
 * @param[in] seconds         Time in @p domain, s.
 * @param[in] domain          Elapsed or epoch seconds.
 * @param[in] utcOffsetHours  Hours added to UTC for epoch times.
 */
std::string formatTime(double seconds, sensordata::TimeDomain domain, int utcOffsetHours);

/**
 * @brief   A value with two decimals, as `QString::arg(value, 0, 'f', 2)`
 *          writes it.
 */
std::string formatValue2(double value);

/**
 * @brief   The y range the range dialog starts from for a stream without a
 *          custom range: its fixed default range, else its data range padded
 *          by 5%, widened when the data is flat; 0 to 1 without data.
 * @param[in] data    The log's plot data; may be null.
 * @param[in] stream  The stream.
 */
PlotRange defaultRangeForStream(const sensordata::Log *data, const StreamMeta &stream);

/**
 * @class   Session
 * @brief   One opened log: catalog, view state and transforms, and the rules over them.
 */
class Session
{
public:
    /// An empty session: no log, default view state, the default transforms.
    Session();

    /**
     * @brief   Opens a log and does everything File > Load does.
     *
     * @details Reads the log (openLog()), resets the view for its streams
     *          with the file name as title, runs the transforms, and applies
     *          @p preferences' entry for the tag, if any. Session settings
     *          (UTC offset, parameters, cursors) carry over from the previous
     *          log, as in the Qt app.
     *
     * @param[in]  path         The log, UTF-8.
     * @param[in]  preferences  Per-tag preferences to apply; may be null.
     * @param[out] error        Why the log could not be opened; set only on failure.
     * @return  false, leaving the session as it was, when openLog() fails;
     *          transform failures are not failures of the open, and are
     *          reported through transformFailures().
     */
    bool open(const std::string &path, const PreferenceStore *preferences, std::string &error);

    /// Takes @p catalog, read by openLog() elsewhere, as the session's log.
    void adopt(StreamCatalog catalog) { catalog_ = std::move(catalog); }

    /// The opened log; its path is empty when none is.
    const StreamCatalog &catalog() const { return catalog_; }
    /// The opened log, for the Qt adapter's loader.
    StreamCatalog &catalog() { return catalog_; }
    /// The view state.
    const ViewModel &view() const { return view_; }
    /// The view state, for setters with no rule of the session's own.
    ViewModel &view() { return view_; }
    /// The transforms.
    const TransformRegistry &transforms() const { return transforms_; }

    /// The key preferences are stored under: the tag type, else the profile name.
    std::string preferenceKey() const;

    /**
     * @brief   Applies @p preferences' entry for this log's tag, or the
     *          defaults when it has none, as File > Load and Preferences >
     *          Load do: the overrides with the gap mode kept, then the gap
     *          mode. Nothing without a key or streams.
     * @return  Changes made.
     */
    ViewModel::Changes applyPreferences(const PreferenceStore &preferences);

    /**
     * @brief   Stores this log's display choices in @p preferences under its
     *          tag, or removes the entry when they are the defaults. Nothing
     *          without a key or streams.
     */
    void rememberPreferences(PreferenceStore &preferences) const;

    /**
     * @brief   Whether stream @p id is shown, as a transform remaking it
     *          keeps it: shown when absent or derived, else its visibility.
     */
    bool shown(const std::string &id) const;

    // -- Transforms ---------------------------------------------------------

    /**
     * @brief   Runs every applicable transform, publishing into @p sink.
     * @return  (transform id, error) for each that failed.
     */
    std::vector<std::pair<std::string, std::string>> runTransforms(TransformSink &sink);

    /// The failures of the last open() or runTransforms().
    const std::vector<std::pair<std::string, std::string>> &transformFailures() const
    {
        return failures_;
    }

    /**
     * @brief   Sets a transform parameter and remakes what it affects.
     * @param[in,out] sink   Where remade outputs go.
     * @param[in]     id     Parameter id (ViewModel::kSeaLevelPressure, ...).
     * @param[in]     value  The value; a toggle is on when non-zero.
     * @param[out]    error  Why a remake failed; set only on failure.
     */
    bool setParameter(TransformSink &sink, const std::string &id, double value, std::string &error);

    /// setParameter() publishing into the session's own view (as open() does).
    bool setParameter(const std::string &id, double value, std::string &error);

    // -- Ranges -------------------------------------------------------------

    /**
     * @brief   Sets a y range the user chose, and the ranges linked to it.
     *
     * @details The range is explicit for @p id. The other members of its
     *          axis group get it too, since a shared axis takes its first
     *          member's range. A pressure range gives altitude the matching
     *          range, and activity and its filter share theirs, unless the
     *          other has an explicit range of its own.
     *
     * @return  Changes made; None when @p id is not a stream.
     */
    ViewModel::Changes setStreamRange(const std::string &id, double lower, double upper);

    /// The range the range dialog starts from for @p id: custom, else defaultRangeForStream().
    PlotRange rangeForDialog(const std::string &id) const;

    /// Altitude for @p pressureMbar at the current sea-level pressure, m.
    double altitudeFromPressure(double pressureMbar) const;

    /**
     * @brief   Reset Zoom: clears every custom range.
     * @return  Changes made; None when nothing is plotted.
     */
    ViewModel::Changes resetZoom();

    // -- Plot and readout ---------------------------------------------------

    /// What the plot policy needs from the log.
    PlotLogInfo plotLogInfo() const;

    /// The current PlotSpec.
    PlotSpec plotSpec() const { return view_.plotSpec(plotLogInfo()); }

    /**
     * @brief   One stream's hover text at @p time: `label: value units`, the
     *          value with two decimals, from the sample at or after @p time.
     * @return  Empty when there is no such sample, or @p time is in a gap
     *          while lines break at gaps.
     */
    std::string hoverValue(const StreamMeta &stream, double time) const;

    /// formatTime() in the log's time domain and the view's UTC offset.
    std::string formatTime(double time) const;

    /**
     * @brief   The hover readout at @p time: the time, then each visible
     *          stream's value, in drawing order, skipping those without one.
     */
    std::vector<std::string> hover(double time) const;

    /// The compass transform's orientation samples; empty when there is no compass.
    const std::vector<OrientationSample> &compassSamples() const;

private:
    StreamCatalog catalog_;                                  ///< The opened log.
    ViewModel view_;                                         ///< The view state.
    TransformRegistry transforms_ = TransformRegistry::makeDefault(); ///< The transforms.
    std::vector<std::pair<std::string, std::string>> failures_; ///< Last transform failures.
};

} // namespace sensorviz

#endif
