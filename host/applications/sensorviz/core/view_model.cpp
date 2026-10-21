/**
 * @file    view_model.cpp
 * @brief   sensorViz view state: streams, visibility, ranges, appearance, settings and preferences.
 */
#include "view_model.h"

#include <algorithm>

namespace sensorviz {

namespace {

/**
 * @brief   Inputs a derived stream is listed after, so it sits beside them.
 *
 * @details Ordering only; the transforms that make these streams live in
 *          the front end.
 */
std::vector<std::string> sourceOrderForStream(const std::string &id)
{
    if (id == "altitude") {
        return {"pressure", "imu_pressure"};
    }
    if (id == "imu_accel_magnitude") {
        return {"imu_ax", "imu_ay", "imu_az"};
    }
    if (id == "imu_gyro_magnitude") {
        return {"imu_gx", "imu_gy", "imu_gz"};
    }
    if (id == "imu_mag_magnitude") {
        return {"imu_mx", "imu_my", "imu_mz"};
    }
    return {};
}

} // namespace

void ViewModel::setListener(Listener listener)
{
    listener_ = std::move(listener);
}

ViewModel::Changes ViewModel::notify(Changes changes)
{
    if (changes != None && listener_) {
        listener_(changes);
    }
    return changes;
}

ViewModel::Changes ViewModel::resetForLog(std::vector<StreamMeta> streams, const std::string &title)
{
    streams_ = std::move(streams);
    visible_.clear();
    for (const StreamMeta &stream : streams_) {
        if (!stream.derived && stream.defaultVisible) {
            visible_.insert(stream.id);
        }
    }
    custom_ranges_.clear();
    explicit_ranges_.clear();
    colors_.clear();
    sides_.clear();
    title_ = title;
    title_visible_ = true;
    return notify(Streams | Visibility | Ranges | Appearance | Title);
}

const StreamMeta *ViewModel::stream(const std::string &id) const
{
    for (const StreamMeta &candidate : streams_) {
        if (candidate.id == id) {
            return &candidate;
        }
    }
    return nullptr;
}

ViewModel::StreamUpdate ViewModel::addOrReplaceStream(const StreamMeta &stream, bool visible)
{
    StreamUpdate update;
    update.changes = Streams;
    for (StreamMeta &existing : streams_) {
        if (existing.id != stream.id) {
            continue;
        }
        if (existing.dataId != stream.dataId) {
            update.released = existing.dataId;
        }
        existing = stream;
        update.replaced = true;
        break;
    }
    if (!update.replaced) {
        // Insert just after the last of the stream's inputs, if any are present.
        const std::vector<std::string> sources = sourceOrderForStream(stream.id);
        auto position = streams_.end();
        for (auto it = streams_.begin(); it != streams_.end(); ++it) {
            if (std::find(sources.begin(), sources.end(), it->id) != sources.end()) {
                position = it + 1;
            }
        }
        streams_.insert(position, stream);
    }
    if (!stream.derived) {
        const bool was_visible = visible_.count(stream.id) != 0;
        if (visible) {
            visible_.insert(stream.id);
        } else {
            visible_.erase(stream.id);
        }
        if (was_visible != visible || !update.replaced) {
            update.changes |= Visibility;
        }
    } else if (!update.replaced) {
        update.changes |= Visibility; // A new derived stream is shown.
    }
    notify(update.changes);
    return update;
}

sensordata::StreamId ViewModel::removeStream(const std::string &id)
{
    // A future stream with the same id, such as altitude re-made with a new
    // sea-level pressure, must not inherit a stale range.
    for (auto it = streams_.begin(); it != streams_.end(); ++it) {
        if (it->id != id) {
            continue;
        }
        const sensordata::StreamId data = it->dataId;
        streams_.erase(it);
        visible_.erase(id);
        custom_ranges_.erase(id);
        explicit_ranges_.erase(id);
        notify(Streams | Visibility | Ranges);
        return data;
    }
    return sensordata::kNoStream;
}

std::vector<std::string> ViewModel::toggleableStreamIds() const
{
    std::vector<std::string> ids;
    for (const StreamMeta &stream : streams_) {
        if (!stream.derived) {
            ids.push_back(stream.id);
        }
    }
    return ids;
}

bool ViewModel::isVisible(const std::string &id) const
{
    const StreamMeta *found = stream(id);
    return found && (found->derived || visible_.count(id) != 0);
}

ViewModel::Changes ViewModel::setStreamVisible(const std::string &id, bool visible)
{
    const StreamMeta *found = stream(id);
    if (!found || found->derived || (visible_.count(id) != 0) == visible) {
        return None;
    }
    if (visible) {
        visible_.insert(id);
    } else {
        visible_.erase(id);
    }
    return notify(Visibility);
}

ViewModel::Changes ViewModel::setVisibleStreams(const std::set<std::string> &ids)
{
    std::set<std::string> wanted;
    for (const StreamMeta &stream : streams_) {
        if (!stream.derived && ids.count(stream.id) != 0) {
            wanted.insert(stream.id);
        }
    }
    if (wanted == visible_) {
        return None;
    }
    visible_ = wanted;
    return notify(Visibility);
}

std::vector<std::string> ViewModel::visibleStreamIds() const
{
    std::vector<std::string> ids;
    for (const StreamMeta &stream : streams_) {
        if (!stream.derived && visible_.count(stream.id) != 0) {
            ids.push_back(stream.id);
        }
    }
    for (const StreamMeta &stream : streams_) {
        if (stream.derived) {
            ids.push_back(stream.id);
        }
    }
    return ids;
}

ViewModel::Changes ViewModel::setCustomRange(const std::string &id, const PlotRange &range,
                                             bool explicitly)
{
    const auto existing = custom_ranges_.find(id);
    const bool same = existing != custom_ranges_.end() && existing->second == range
        && (!explicitly || explicit_ranges_.count(id) != 0);
    if (same) {
        return None;
    }
    custom_ranges_[id] = range;
    if (explicitly) {
        explicit_ranges_.insert(id);
    }
    return notify(Ranges);
}

ViewModel::Changes ViewModel::clearCustomRanges()
{
    if (custom_ranges_.empty() && explicit_ranges_.empty()) {
        return None;
    }
    custom_ranges_.clear();
    explicit_ranges_.clear();
    return notify(Ranges);
}

ViewModel::Changes ViewModel::setColorOverrides(const std::map<std::string, Rgb> &colors)
{
    if (colors == colors_) {
        return None;
    }
    colors_ = colors;
    return notify(Appearance);
}

Rgb ViewModel::effectiveColor(const std::string &id) const
{
    const auto override_color = colors_.find(id);
    if (override_color != colors_.end()) {
        return override_color->second;
    }
    const StreamMeta *found = stream(id);
    return found ? found->color : Rgb{80, 100, 140};
}

ViewModel::Changes ViewModel::setAxisSideOverrides(const std::map<std::string, AxisSide> &sides)
{
    if (sides == sides_) {
        return None;
    }
    sides_ = sides;
    return notify(Appearance);
}

AxisSide ViewModel::effectiveAxisSide(const std::string &id) const
{
    const auto override_side = sides_.find(id);
    if (override_side != sides_.end()) {
        return override_side->second;
    }
    const StreamMeta *found = stream(id);
    return found ? found->axisSide : AxisSide::Left;
}

ViewModel::Changes ViewModel::setTitle(const std::string &title)
{
    if (title == title_) {
        return None;
    }
    title_ = title;
    return notify(Title);
}

ViewModel::Changes ViewModel::setTitleVisible(bool visible)
{
    if (visible == title_visible_) {
        return None;
    }
    title_visible_ = visible;
    return notify(Title);
}

ViewModel::Changes ViewModel::setUtcOffsetHours(int hours)
{
    if (hours == utc_offset_hours_) {
        return None;
    }
    utc_offset_hours_ = hours;
    return notify(UtcOffset);
}

ViewModel::Changes ViewModel::setCursorsVisible(bool visible)
{
    if (visible == cursors_visible_) {
        return None;
    }
    cursors_visible_ = visible;
    return notify(Cursors);
}

ViewModel::Changes ViewModel::setBreakAtGaps(bool breakAtGaps)
{
    if (breakAtGaps == break_at_gaps_) {
        return None;
    }
    break_at_gaps_ = breakAtGaps;
    return notify(Gaps);
}

ViewModel::Changes ViewModel::setSeaLevelPressure(double mbar)
{
    if (mbar == sea_level_pressure_) {
        return None;
    }
    sea_level_pressure_ = mbar;
    return notify(Parameters);
}

ViewModel::Changes ViewModel::setDeclinationDegrees(double degrees)
{
    if (degrees == declination_degrees_) {
        return None;
    }
    declination_degrees_ = degrees;
    return notify(Parameters);
}

ViewModel::Changes ViewModel::setBatteryForward(bool forward)
{
    if (forward == battery_forward_) {
        return None;
    }
    battery_forward_ = forward;
    return notify(Parameters);
}

ViewModel::Changes ViewModel::setActivityTimeConstant(double seconds)
{
    if (seconds == activity_time_constant_) {
        return None;
    }
    activity_time_constant_ = seconds;
    return notify(Parameters);
}

std::set<std::string> ViewModel::defaultVisibleIds() const
{
    // Every stream whose default is visible, derived ones included, as the
    // preference rule has always compared against.
    std::set<std::string> ids;
    for (const StreamMeta &stream : streams_) {
        if (stream.defaultVisible) {
            ids.insert(stream.id);
        }
    }
    return ids;
}

TagPreferences ViewModel::capturePreferences(const std::string &tagType) const
{
    TagPreferences preferences;
    preferences.tagType = tagType;
    if (visible_ != defaultVisibleIds()) {
        preferences.hasVisibleStreamOverride = true;
        preferences.visibleStreamIds = visible_;
    }
    for (const auto &entry : colors_) {
        const StreamMeta *found = stream(entry.first);
        if (found && entry.second != found->color) {
            preferences.streamColors[entry.first] = entry.second;
        }
    }
    for (const auto &entry : sides_) {
        const StreamMeta *found = stream(entry.first);
        if (found && entry.second != found->axisSide) {
            preferences.axisSides[entry.first] = entry.second;
        }
    }
    if (!break_at_gaps_) {
        preferences.hasBreakLinesAtGapsOverride = true;
        preferences.breakLinesAtGaps = false;
    }
    return preferences;
}

ViewModel::Changes ViewModel::applyPreferences(const TagPreferences &preferences)
{
    // Apply quietly, then report everything that changed once.
    const Listener listener = std::move(listener_);
    listener_ = nullptr;

    Changes changes = None;
    std::map<std::string, Rgb> colors;
    for (const auto &entry : preferences.streamColors) {
        if (hasStream(entry.first)) {
            colors[entry.first] = entry.second;
        }
    }
    changes |= setColorOverrides(colors);

    std::map<std::string, AxisSide> sides;
    for (const auto &entry : preferences.axisSides) {
        if (hasStream(entry.first)) {
            sides[entry.first] = entry.second;
        }
    }
    changes |= setAxisSideOverrides(sides);

    changes |= setVisibleStreams(preferences.hasVisibleStreamOverride ? preferences.visibleStreamIds
                                                                      : defaultVisibleIds());
    changes |= setBreakAtGaps(
        preferences.hasBreakLinesAtGapsOverride ? preferences.breakLinesAtGaps : true);

    listener_ = listener;
    return notify(changes);
}

PlotState ViewModel::plotState() const
{
    PlotState state;
    state.visibleStreamIds = visibleStreamIds();
    state.customRanges = custom_ranges_;
    state.utcOffsetHours = utc_offset_hours_;
    state.title = title_;
    state.titleVisible = title_visible_;
    state.declinationDegrees = declination_degrees_;
    state.seaLevelPressure = sea_level_pressure_;
    state.breakAtGaps = break_at_gaps_;
    return state;
}

PlotDisplayPreferences ViewModel::displayPreferences() const
{
    PlotDisplayPreferences preferences;
    preferences.colors = colors_;
    preferences.axisSides = sides_;
    return preferences;
}

PlotSpec ViewModel::plotSpec(const PlotLogInfo &log) const
{
    return buildPlotSpec(log, streams_, displayPreferences(), plotState());
}

} // namespace sensorviz
