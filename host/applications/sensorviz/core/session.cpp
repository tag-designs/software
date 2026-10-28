/**
 * @file    session.cpp
 * @brief   Session, and the readout text and range rules moved from the Qt
 *          front end.
 *
 * @details formatTime(), formatValue2(), the hover text, the default and
 *          linked ranges and Reset Zoom were MainWindow code; each is moved
 *          step for step, and the text matches Qt's formatting (QString::arg
 *          with 'f', QDateTime::toString()), checked against Qt itself when
 *          it moved.
 */
#include "session.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <vector>

namespace sensorviz {
namespace {

/**
 * @brief   The civil date of a day count from 1970-01-01, proleptic Gregorian.
 * @details Howard Hinnant's civil_from_days(), valid for any day count.
 */
void civilFromDays(std::int64_t days, int &year, unsigned &month, unsigned &day)
{
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned dayOfEra = static_cast<unsigned>(days - era * 146097);
    const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const unsigned monthIndex = (5 * dayOfYear + 2) / 153;
    day = dayOfYear - (153 * monthIndex + 2) / 5 + 1;
    month = monthIndex < 10 ? monthIndex + 3 : monthIndex - 9;
    year = static_cast<int>(yearOfEra + era * 400 + (month <= 2));
}

/// The session's own sink: outputs go straight into its view.
class ViewSink : public TransformSink
{
public:
    /// A sink into @p view, releasing replaced data from @p catalog's log.
    ViewSink(ViewModel &view, const StreamCatalog &catalog) : view_(view), catalog_(catalog) {}

    /// Adds or replaces the output in the view, freeing the replaced data.
    void publish(const StreamMeta &stream, bool visible) override
    {
        const ViewModel::StreamUpdate update = view_.addOrReplaceStream(stream, visible);
        if (update.released != sensordata::kNoStream && catalog_.data) {
            catalog_.data->remove(update.released);
        }
    }
    /// Writes the warning to stderr.
    void warn(const std::string &message) override { std::fprintf(stderr, "%s\n", message.c_str()); }

private:
    ViewModel &view_;              ///< Where outputs go.
    const StreamCatalog &catalog_; ///< The log, for releasing replaced data.
};

/// The final path component of @p path, after either kind of slash.
std::string fileName(const std::string &path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

std::string formatValue2(double value)
{
    if (std::isnan(value) || std::isinf(value)) {
        char text[16];
        std::snprintf(text, sizeof text, "%.2f", value);
        return text;
    }
    // Qt rounds the exact decimal value half away from zero, where printf
    // rounds an exact tie to even (0.125 is "0.13" to Qt), and writes a
    // negative zero without its sign. A double's exact decimal expansion is
    // finite, at most 1074 fractional digits, so print it in full and round
    // by hand.
    std::vector<char> exact(1400);
    std::snprintf(exact.data(), exact.size(), "%.1100f", std::fabs(value));
    std::string digits(exact.data());
    const size_t point = digits.find('.');
    std::string whole = digits.substr(0, point);
    std::string fraction = digits.substr(point + 1, 2);
    if (digits[point + 3] >= '5') {
        std::string all = whole + fraction;
        int i = static_cast<int>(all.size()) - 1;
        while (i >= 0 && all[static_cast<size_t>(i)] == '9') {
            all[static_cast<size_t>(i)] = '0';
            i--;
        }
        if (i < 0) {
            all.insert(all.begin(), '1');
        } else {
            all[static_cast<size_t>(i)]++;
        }
        whole = all.substr(0, all.size() - 2);
        fraction = all.substr(all.size() - 2);
    }
    const bool negative = value < 0.0;
    return (negative ? "-" : "") + whole + "." + fraction;
}

std::string formatTime(double seconds, sensordata::TimeDomain domain, int utcOffsetHours)
{
    char text[96];
    if (domain == sensordata::TimeDomain::ElapsedSeconds) {
        const bool negative = seconds < 0.0;
        double remaining = std::abs(seconds);
        const int hours = static_cast<int>(remaining / 3600.0);
        remaining -= hours * 3600.0;
        const int minutes = static_cast<int>(remaining / 60.0);
        remaining -= minutes * 60.0;
        const char *prefix = negative ? "-" : "";
        if (hours > 0) {
            std::snprintf(text, sizeof text, "%sElapsed %d:%02d:%06.3f s", prefix, hours, minutes,
                          remaining);
        } else {
            std::snprintf(text, sizeof text, "%sElapsed %d:%06.3f s", prefix, minutes, remaining);
        }
        return text;
    }

    // QDateTime::fromSecsSinceEpoch(qint64(seconds), offset): the seconds
    // truncated toward zero, then shown in the offset's local time.
    const std::int64_t local = static_cast<std::int64_t>(seconds) + std::int64_t{3600} * utcOffsetHours;
    const std::int64_t days = local >= 0 ? local / 86400 : (local - 86399) / 86400;
    const std::int64_t secondOfDay = local - days * 86400;
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civilFromDays(days, year, month, day);
    std::snprintf(text, sizeof text, "%02u/%02u %02d:%02d:%02d", month, day,
                  static_cast<int>(secondOfDay / 3600), static_cast<int>(secondOfDay / 60 % 60),
                  static_cast<int>(secondOfDay % 60));
    return text;
}

PlotRange defaultRangeForStream(const sensordata::Log *data, const StreamMeta &stream)
{
    if (stream.axisRange.enabled) {
        return PlotRange{stream.axisRange.lower, stream.axisRange.upper};
    }
    PlotRange range;
    if (!plotDataRange(data, stream.dataId, range)) {
        return PlotRange{0.0, 1.0};
    }
    double lower = range.lower;
    double upper = range.upper;
    double span = upper - lower;
    if (span <= 0.0) {
        span = std::max(1.0, std::abs((lower + upper) * 0.5) * 0.1);
        const double center = (lower + upper) * 0.5;
        lower = center - span * 0.5;
        upper = center + span * 0.5;
    }
    const double margin = span * 0.05;
    return PlotRange{lower - margin, upper + margin};
}

Session::Session() = default;

bool Session::open(const std::string &path, const PreferenceStore *preferences, std::string &error)
{
    StreamCatalog catalog;
    if (!openLog(path, catalog, error)) {
        return false;
    }
    catalog_ = std::move(catalog);
    view_.resetForLog(catalog_.streams, fileName(path));
    ViewSink sink(view_, catalog_);
    runTransforms(sink);

    // As the Qt app applies them: nothing without a key or streams; else the
    // overrides with the gap mode kept, then the gap mode.
    if (preferenceKey().empty() || view_.streams().empty()) {
        return true;
    }
    const TagPreferences tagPreferences =
        preferences ? preferences->get(preferenceKey()) : TagPreferences();
    TagPreferences firstPhase = tagPreferences;
    firstPhase.hasBreakLinesAtGapsOverride = true;
    firstPhase.breakLinesAtGaps = view_.breakAtGaps();
    view_.applyPreferences(firstPhase);
    view_.setBreakAtGaps(
        tagPreferences.hasBreakLinesAtGapsOverride ? tagPreferences.breakLinesAtGaps : true);
    return true;
}

std::string Session::preferenceKey() const
{
    return catalog_.tagType.empty() ? catalog_.profileName : catalog_.tagType;
}

bool Session::shown(const std::string &id) const
{
    const StreamMeta *stream = view_.stream(id);
    return !stream || stream->derived || view_.isVisible(stream->id);
}

std::vector<std::pair<std::string, std::string>> Session::runTransforms(TransformSink &sink)
{
    TransformContext context(catalog_, view_, sink);
    failures_ = transforms_.runAll(context);
    return failures_;
}

bool Session::setParameter(TransformSink &sink, const std::string &id, double value,
                           std::string &error)
{
    TransformContext context(catalog_, view_, sink);
    return transforms_.setParameter(context, id, value, error);
}

bool Session::setParameter(const std::string &id, double value, std::string &error)
{
    ViewSink sink(view_, catalog_);
    return setParameter(sink, id, value, error);
}

ViewModel::Changes Session::setStreamRange(const std::string &id, double lower, double upper)
{
    const StreamMeta *stream = view_.stream(id);
    if (!stream) {
        return ViewModel::None;
    }
    ViewModel::Changes changes = view_.setCustomRange(id, PlotRange{lower, upper}, true);

    // Streams in one axis group share one axis, which takes the first
    // member's custom range; give them all this one so the axis follows it.
    const std::string groupKey = axisGroupKey(*stream);
    if (!groupKey.empty()) {
        for (const StreamMeta &member : view_.streams()) {
            if (member.id != id && axisGroupKey(member) == groupKey) {
                changes |= view_.setCustomRange(member.id, PlotRange{lower, upper}, true);
            }
        }
    }

    // Altitude is derived directly from pressure: keep it aligned unless it
    // has an explicit range of its own.
    if ((id == "pressure" || id == "imu_pressure") && view_.hasStream("altitude")
        && !view_.isExplicitRange("altitude")) {
        changes |= view_.setCustomRange(
            "altitude", PlotRange{altitudeFromPressure(upper), altitudeFromPressure(lower)}, false);
    }
    if (id == "activity" && view_.hasStream("activity_lowpass")
        && !view_.isExplicitRange("activity_lowpass")) {
        changes |= view_.setCustomRange("activity_lowpass", PlotRange{lower, upper}, false);
    } else if (id == "activity_lowpass" && view_.hasStream("activity")
               && !view_.isExplicitRange("activity")) {
        changes |= view_.setCustomRange("activity", PlotRange{lower, upper}, false);
    }
    return changes;
}

PlotRange Session::rangeForDialog(const std::string &id) const
{
    if (view_.hasCustomRange(id)) {
        return view_.customRanges().at(id);
    }
    const StreamMeta *stream = view_.stream(id);
    return stream ? defaultRangeForStream(catalog_.data.get(), *stream) : PlotRange{0.0, 1.0};
}

double Session::altitudeFromPressure(double pressureMbar) const
{
    return sensorviz::altitudeFromPressure(pressureMbar, view_.seaLevelPressure());
}

ViewModel::Changes Session::resetZoom()
{
    if (plotSpec().traces.empty()) {
        return ViewModel::None;
    }
    return view_.clearCustomRanges();
}

PlotLogInfo Session::plotLogInfo() const
{
    PlotLogInfo info;
    info.timeDomain = catalog_.timeDomain;
    info.hasCollectionStart = catalog_.hasCollectionStart;
    info.collectionStartEpochMs = catalog_.collectionStartEpochMs;
    for (const EventMarker &marker : catalog_.eventMarkers) {
        if (marker.timeDomain == catalog_.timeDomain) {
            info.eventMarkerTimes.insert(info.eventMarkerTimes.end(), marker.time.cbegin(),
                                         marker.time.cend());
        }
    }
    info.data = catalog_.data.get();
    return info;
}

std::string Session::hoverValue(const StreamMeta &stream, double time) const
{
    if (!catalog_.data) {
        return {};
    }
    // Inside a gap there is no value to show, unless lines are joined across gaps.
    const std::optional<sensordata::Sample> sample = catalog_.data->valueAt(
        stream.dataId, time, sensordata::Pick::AtOrAfter,
        view_.breakAtGaps() ? sensordata::GapMode::Break : sensordata::GapMode::Join);
    if (!sample) {
        return {};
    }
    return stream.label + ": " + formatValue2(sample->value)
        + (stream.units.empty() ? std::string() : " " + stream.units);
}

std::string Session::formatTime(double time) const
{
    return sensorviz::formatTime(time, catalog_.timeDomain, view_.utcOffsetHours());
}

std::vector<std::string> Session::hover(double time) const
{
    std::vector<std::string> rows{formatTime(time)};
    for (const std::string &id : view_.visibleStreamIds()) {
        if (const StreamMeta *stream = view_.stream(id)) {
            std::string value = hoverValue(*stream, time);
            if (!value.empty()) {
                rows.push_back(std::move(value));
            }
        }
    }
    return rows;
}

const std::vector<OrientationSample> &Session::compassSamples() const
{
    static const std::vector<OrientationSample> none;
    const CompassTransform *compass = transforms_.compass();
    return compass ? compass->samples() : none;
}

} // namespace sensorviz
