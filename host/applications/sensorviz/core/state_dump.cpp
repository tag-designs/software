/**
 * @file    state_dump.cpp
 * @brief   The state dump's script and text, shared by the Qt app's
 *          `--dump-state` and `session_cli`.
 *
 * @details Moved from the Qt app's `state_dump.cpp`; the text is the same,
 *          character for character, so dumps made before and after the move
 *          compare.
 */
#include "state_dump.h"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace sensorviz {
namespace {

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
    std::string text() const
    {
        char out[24];
        std::snprintf(out, sizeof out, "%016" PRIx64, state_);
        return out;
    }

private:
    std::uint64_t state_ = 0xcbf29ce484222325ULL; ///< FNV offset basis.
};

/// @p value exactly, as a C hex float.
std::string hex(double value)
{
    char text[40];
    std::snprintf(text, sizeof text, "%a", value);
    return text;
}

/// A color as `#rrggbb`.
std::string rgb(Rgb color)
{
    char text[8];
    std::snprintf(text, sizeof text, "#%02x%02x%02x", color.r, color.g, color.b);
    return text;
}

/// @p value as decimal.
template <typename T>
std::string number(T value)
{
    return std::to_string(value);
}

/// A bool as `1` or `0`.
std::string flag(bool value)
{
    return value ? "1" : "0";
}

/// A hash of the stream's M4 envelope at @p columns over its whole range, as `points:hash`.
std::string envelopeHash(const sensordata::Log &log, sensordata::StreamId stream, int columns,
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
    return number(envelope.time.size()) + ":" + hash.text();
}

} // namespace

std::string stateDumpStep(const Session &session, const std::string &step)
{
    const StreamCatalog &catalog = session.catalog();
    const ViewModel &view = session.view();
    std::vector<std::string> lines;
    lines.push_back("== " + step);

    const PlotSpec spec = session.plotSpec();
    lines.push_back("spec domain=" + number(static_cast<int>(spec.timeDomain))
                    + " utc=" + number(spec.utcOffsetHours) + " x=" + flag(spec.hasXDataRange) + " "
                    + hex(spec.xDataRange.lower) + ".." + hex(spec.xDataRange.upper) + " title=["
                    + spec.title + "] gaps=" + flag(spec.breakAtGaps));
    for (const std::string &row : spec.metadataRows) {
        lines.push_back("meta [" + row + "]");
    }
    Hash markers;
    for (double time : spec.eventMarkerTimes) {
        markers.add(time);
    }
    lines.push_back("markers " + number(spec.eventMarkerTimes.size()) + " " + markers.text());
    for (const PlotAxisSpec &axis : spec.axes) {
        std::string traces;
        for (size_t i = 0; i < axis.traces.size(); i++) {
            traces += (i ? "," : "") + number(axis.traces[i]);
        }
        lines.push_back("axis side=" + number(static_cast<int>(axis.side)) + " [" + axis.label + "] "
                        + rgb(axis.color) + " mode=" + number(static_cast<int>(axis.mode))
                        + " group=" + axis.linkedGroup + " range=" + flag(axis.hasRange) + " "
                        + hex(axis.lower) + ".." + hex(axis.upper) + " traces=" + traces);
    }
    for (const PlotTraceSpec &trace : spec.traces) {
        const std::string data = catalog.data && trace.stream != sensordata::kNoStream
            ? envelopeHash(*catalog.data, trace.stream, 1200,
                           spec.breakAtGaps ? sensordata::GapMode::Break : sensordata::GapMode::Join)
            : "none";
        lines.push_back("trace " + trace.id + " [" + trace.name + "] " + rgb(trace.color)
                        + " pen=" + number(trace.penWidth) + " axis=" + number(trace.axis)
                        + " data=" + data);
    }

    for (const StreamMeta &stream : view.streams()) {
        lines.push_back("stream " + stream.id + " [" + stream.label + "] [" + stream.units
                        + "] group=" + stream.group + " [" + stream.groupLabel + "] "
                        + rgb(stream.color) + " derived=" + flag(stream.derived)
                        + " default=" + flag(stream.defaultVisible)
                        + " side=" + number(static_cast<int>(stream.axisSide))
                        + " range=" + flag(stream.axisRange.enabled) + " "
                        + hex(stream.axisRange.lower) + ".." + hex(stream.axisRange.upper)
                        + " visible=" + flag(session.shown(stream.id))
                        + " domain=" + number(static_cast<int>(stream.timeDomain)));
        const sensordata::StreamInfo *info = catalog.data ? catalog.data->info(stream.dataId) : nullptr;
        if (info) {
            // One column per sample holds every sample of an evenly spaced stream.
            const int every = static_cast<int>(std::min<std::size_t>(info->samples, 4000000));
            lines.push_back("  data n=" + number(info->samples) + " t=" + hex(info->first_time) + ".."
                            + hex(info->last_time) + " v=" + hex(info->min) + ".." + hex(info->max)
                            + " gaps=" + number(info->gaps) + " all="
                            + envelopeHash(*catalog.data, stream.dataId, every, sensordata::GapMode::Join)
                            + " view="
                            + envelopeHash(*catalog.data, stream.dataId, 1200, sensordata::GapMode::Break));
        } else {
            lines.push_back("  data none");
        }
    }

    for (const auto &[id, range] : view.customRanges()) {
        lines.push_back("range " + id + " " + hex(range.lower) + ".." + hex(range.upper)
                        + " explicit=" + flag(view.isExplicitRange(id)));
    }

    // Hover rows: the time, then every stream's value text, empty or not.
    if (spec.hasXDataRange) {
        const int points = 24;
        for (int k = 0; k <= points; k++) {
            const double time = spec.xDataRange.lower
                + (spec.xDataRange.upper - spec.xDataRange.lower) * k / points;
            std::string row = "hover " + session.formatTime(time);
            for (const StreamMeta &stream : view.streams()) {
                row += " | " + session.hoverValue(stream, time);
            }
            lines.push_back(row);
        }
    }

    Hash compass;
    for (const OrientationSample &sample : session.compassSamples()) {
        compass.add(sample.time);
        compass.add(sample.q.w);
        compass.add(sample.q.x);
        compass.add(sample.q.y);
        compass.add(sample.q.z);
        compass.add(sample.dip);
        compass.add(sample.field);
        compass.add(sample.mg);
    }
    const CompassTransform *compassTransform = session.transforms().compass();
    lines.push_back("compass " + number(session.compassSamples().size()) + " " + compass.text() + " ["
                    + (compassTransform ? compassTransform->source() : std::string()) + "]");

    std::string text;
    for (const std::string &line : lines) {
        text += line + "\n";
    }
    return text;
}

std::string runStateDump(const Session &session, StateDumpDriver &driver)
{
    std::string dump = stateDumpStep(session, "loaded");

    driver.setSeaLevelPressure(1000.0);
    dump += stateDumpStep(session, "sea level 1000");

    const std::string pressure = session.view().hasStream("pressure") ? "pressure" : "imu_pressure";
    if (session.view().hasStream(pressure)) {
        driver.setStreamRange(pressure, 950.0, 1010.0);
        dump += stateDumpStep(session, "pressure range 950..1010");
    }

    driver.setDeclination(12.5);
    dump += stateDumpStep(session, "declination 12.5");

    driver.toggleBatteryForward();
    dump += stateDumpStep(session, "battery forward toggled");

    driver.joinGaps();
    dump += stateDumpStep(session, "gaps joined");

    if (session.catalog().timeDomain == sensordata::TimeDomain::EpochSeconds) {
        driver.setUtcOffset(-5);
        dump += stateDumpStep(session, "utc -5");
    }

    driver.resetZoom();
    dump += stateDumpStep(session, "reset zoom");

    // Last, because only the Qt app has the activity filter: dumps from other
    // front ends match the Qt app's up to here.
    if (session.view().hasStream("activity") && driver.enableActivityFilter(600.0)) {
        dump += stateDumpStep(session, "activity filter 600");
    }
    return dump;
}

} // namespace sensorviz
