/**
 * @file    plotspec_check.cc
 * @brief   Offline assertion checks for buildPlotSpec(), sensorViz's plot policy.
 *
 * @details Loads the committed sensorviz fixtures through sensordata, as the
 *          Qt-free core sees them, and checks the PlotSpec built for a range of plot states, with no
 *          window: linked IMU ranges and what breaks a link, fixed and custom
 *          ranges, Auto data ranges, colour and side overrides, the time
 *          extent, event markers, metadata-box rows and the title. Expected
 *          ranges are recomputed here from the sensordata summaries rather
 *          than taken from the code under test.
 *
 *          Exits 0 when every check passes, 1 otherwise; each failure is
 *          printed with its line.
 */
#include "plot_spec.h"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <cmath>
#include <cstdio>

#ifndef TAG_DESIGNS_SOURCE_DIR
/// Repository root, for the fixture paths; CMake passes the real one.
#define TAG_DESIGNS_SOURCE_DIR "."
#endif

namespace
{

int g_failures = 0;
int g_checks = 0;

/// Records one check; prints the failing expression and line.
#define CHECK(condition)                                                              \
    do {                                                                              \
        g_checks++;                                                                   \
        if (!(condition)) {                                                           \
            g_failures++;                                                             \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                             \
    } while (0)

using sensorviz::AxisSide;
using sensorviz::PlotAxisRangeMode;
using sensorviz::PlotAxisSpec;
using sensorviz::PlotDisplayPreferences;
using sensorviz::PlotRange;
using sensorviz::PlotSpec;
using sensorviz::PlotState;
using sensorviz::PlotTraceSpec;
using sensorviz::Rgb;
using sensorviz::StreamMeta;

/// A fixture as the core sees it: its plot data, stored streams and log info.
struct Fixture
{
    std::unique_ptr<sensordata::Log> data;  ///< The log's plot data.
    std::vector<StreamMeta> streams;        ///< Its scalar streams, with display defaults.
    sensorviz::PlotLogInfo info;            ///< What the policy reads about the log.
};

/// A collection start for the elapsed-time fixture; any fixed value does.
constexpr std::int64_t kCollectionStartMs = 1787834147000;

/// Loads a fixture through sensordata, failing the program if it does not load.
Fixture loadFixture(const char *name)
{
    Fixture fixture;
    std::string error;
    const std::string path =
        std::string(TAG_DESIGNS_SOURCE_DIR) + "/host/docs/fixtures/sensorviz/" + name;
    fixture.data = sensordata::Log::open(path, error);
    if (!fixture.data) {
        std::fprintf(stderr, "cannot load %s: %s\n", name, error.c_str());
        std::exit(2);
    }
    for (const sensordata::StreamInfo &info : fixture.data->streams()) {
        if (info.kind != "record_column") {
            fixture.streams.push_back(sensorviz::storedStreamMeta(info));
        }
        if (info.domain == sensordata::TimeDomain::ElapsedSeconds) {
            fixture.info.timeDomain = sensordata::TimeDomain::ElapsedSeconds;
        }
    }
    if (fixture.info.timeDomain == sensordata::TimeDomain::ElapsedSeconds) {
        fixture.info.hasCollectionStart = true;
        fixture.info.collectionStartEpochMs = kCollectionStartMs;
        fixture.info.eventMarkerTimes = {10.0, 20.5};
    }
    fixture.info.data = fixture.data.get();
    return fixture;
}

/// The stream with @p id, or null.
const StreamMeta *find(const Fixture &log, const std::string &id)
{
    for (const StreamMeta &stream : log.streams) {
        if (stream.id == id) {
            return &stream;
        }
    }
    return nullptr;
}

/// Whole-stream range from the sensordata summary, as the spec must use.
PlotRange infoRange(const Fixture &log, const std::string &id)
{
    const sensordata::StreamInfo *info = log.data->info(find(log, id)->dataId);
    return PlotRange{info->min, info->max};
}

/// Index of the trace for @p id, or -1.
int traceIndex(const PlotSpec &spec, const std::string &id)
{
    for (size_t i = 0; i < spec.traces.size(); i++) {
        if (spec.traces[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// The axis of the trace for @p id; the check fails, and a default is returned, when absent.
PlotAxisSpec axisOf(const PlotSpec &spec, const std::string &id)
{
    const int index = traceIndex(spec, id);
    CHECK(index >= 0);
    return index >= 0
        ? spec.axes[static_cast<size_t>(spec.traces[static_cast<size_t>(index)].axis)]
        : PlotAxisSpec{};
}

/// A derived copy of stream @p from registered as @p id, as a transform would make it.
void addDerivedCopy(Fixture &log, const std::string &from, const std::string &id)
{
    StreamMeta stream = *find(log, from);
    sensordata::StreamInfo info;
    info.id = id;
    stream.id = id;
    stream.derived = false;
    stream.dataId = *log.data->addComputed(
        info, {stream.dataId}, [](const float *values) { return static_cast<double>(values[0]); });
    log.streams.push_back(stream);
}

/// True when @p text starts with @p prefix.
bool startsWith(const std::string &text, const std::string &prefix)
{
    return text.compare(0, prefix.size(), prefix) == 0;
}

/// True when @p text ends with @p suffix.
bool endsWith(const std::string &text, const std::string &suffix)
{
    return text.size() >= suffix.size()
        && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void checkImuTag()
{
    Fixture log = loadFixture("imutag.db3");
    CHECK(log.info.timeDomain == sensordata::TimeDomain::ElapsedSeconds);

    PlotState state;
    state.title = "imutag.db3";
    state.visibleStreamIds = {"imu_ax", "imu_ay", "imu_az", "imu_pressure", "no_such_stream"};
    PlotSpec spec = buildPlotSpec(log.info, log.streams, {}, state);

    // Traces in state order, unknown ids ignored, one axis each.
    CHECK(spec.traces.size() == 4 && spec.axes.size() == 4);
    CHECK(traceIndex(spec, "imu_ax") == 0 && traceIndex(spec, "imu_pressure") == 3);
    for (size_t i = 0; i < spec.traces.size(); i++) {
        CHECK(spec.traces[i].axis == static_cast<int>(i));
        CHECK(spec.traces[i].stream == find(log, spec.traces[i].id)->dataId);
        CHECK(spec.traces[i].penWidth == 1);
    }

    // The accel family is linked to the padded union of its data ranges.
    PlotRange accel = infoRange(log, "imu_ax");
    for (const char *id : {"imu_ay", "imu_az"}) {
        const PlotRange r = infoRange(log, id);
        accel.lower = std::min(accel.lower, r.lower);
        accel.upper = std::max(accel.upper, r.upper);
    }
    const PlotRange padded = paddedRange(accel);
    for (const char *id : {"imu_ax", "imu_ay", "imu_az"}) {
        const PlotAxisSpec axis = axisOf(spec, id);
        CHECK(axis.mode == PlotAxisRangeMode::Linked && axis.linkedGroup == "imu_accel");
        CHECK(axis.lower == padded.lower && axis.upper == padded.upper);
        CHECK(axis.side == AxisSide::Right);
        CHECK(axis.color == find(log, id)->color);
    }
    CHECK(axisOf(spec, "imu_ax").label == "Acceleration X mg");

    // Pressure auto-scales on its own data range.
    const PlotAxisSpec pressure = axisOf(spec, "imu_pressure");
    const PlotRange pressure_range = infoRange(log, "imu_pressure");
    CHECK(pressure.mode == PlotAxisRangeMode::Auto && pressure.hasRange);
    CHECK(pressure.lower == pressure_range.lower && pressure.upper == pressure_range.upper);

    // Time extent, markers, metadata and title.
    double first = 1e300;
    double last = -1e300;
    for (const PlotTraceSpec &trace : spec.traces) {
        const sensordata::StreamInfo *info = log.data->info(trace.stream);
        first = std::min(first, info->first_time);
        last = std::max(last, info->last_time);
    }
    CHECK(spec.hasXDataRange && spec.xDataRange.lower == first && spec.xDataRange.upper == last);
    CHECK(spec.eventMarkerTimes == log.info.eventMarkerTimes);
    CHECK(spec.metadataRows.size() == 1 && startsWith(spec.metadataRows[0], "Start:   "));
    CHECK(endsWith(spec.metadataRows[0], " UTC"));
    CHECK(spec.metadataRows[0] == "Start:   " + sensorviz::formatCollectionStart(kCollectionStartMs));
    CHECK(spec.title == "imutag.db3");
    CHECK(spec.breakAtGaps);
    state.breakAtGaps = false;
    CHECK(!buildPlotSpec(log.info, log.streams, {}, state).breakAtGaps);
    state.breakAtGaps = true;
    CHECK(spec.timeDomain == sensordata::TimeDomain::ElapsedSeconds);

    // A custom range fixes one member; the other two stay linked to each other.
    state.customRanges["imu_ay"] = PlotRange{-5.0, 5.0};
    spec = buildPlotSpec(log.info, log.streams, {}, state);
    CHECK(axisOf(spec, "imu_ay").mode == PlotAxisRangeMode::Fixed);
    CHECK(axisOf(spec, "imu_ay").lower == -5.0 && axisOf(spec, "imu_ay").upper == 5.0);
    PlotRange xz = infoRange(log, "imu_ax");
    xz.lower = std::min(xz.lower, infoRange(log, "imu_az").lower);
    xz.upper = std::max(xz.upper, infoRange(log, "imu_az").upper);
    CHECK(axisOf(spec, "imu_ax").mode == PlotAxisRangeMode::Linked);
    CHECK(axisOf(spec, "imu_ax").upper == paddedRange(xz).upper);

    // A family member alone auto-scales.
    state.customRanges.clear();
    state.visibleStreamIds = {"imu_gx", "imu_ax"};
    spec = buildPlotSpec(log.info, log.streams, {}, state);
    CHECK(axisOf(spec, "imu_gx").mode == PlotAxisRangeMode::Auto);
    CHECK(axisOf(spec, "imu_ax").mode == PlotAxisRangeMode::Auto);

    // Preferences override colour and side.
    PlotDisplayPreferences preferences;
    preferences.colors["imu_ax"] = Rgb{1, 2, 3};
    preferences.axisSides["imu_ax"] = AxisSide::Left;
    spec = buildPlotSpec(log.info, log.streams, preferences, state);
    CHECK(axisOf(spec, "imu_ax").color == (Rgb{1, 2, 3}));
    CHECK(spec.traces[static_cast<size_t>(traceIndex(spec, "imu_ax"))].color == (Rgb{1, 2, 3}));
    CHECK(axisOf(spec, "imu_ax").side == AxisSide::Left);
    CHECK(axisOf(spec, "imu_gx").side == AxisSide::Right);

    // Altitude adds the sea-level row; derived streams draw heavier.
    addDerivedCopy(log, "imu_pressure", "altitude");
    log.streams.back().derived = true;
    state.visibleStreamIds = {"altitude"};
    state.seaLevelPressure = 1000.5;
    state.titleVisible = false;
    spec = buildPlotSpec(log.info, log.streams, {}, state);
    CHECK(spec.traces.size() == 1 && spec.traces[0].penWidth == 2);
    CHECK(spec.metadataRows.size() == 2 && spec.metadataRows[1] == "MSL:     1000.50 mbar");
    CHECK(spec.title.empty());

    // Nothing visible: no traces, extent or markers; the start row remains.
    state.visibleStreamIds.clear();
    spec = buildPlotSpec(log.info, log.streams, {}, state);
    CHECK(spec.traces.empty() && spec.axes.empty() && !spec.hasXDataRange);
    CHECK(spec.eventMarkerTimes.empty());
    CHECK(spec.metadataRows.size() == 1);

    // Linked ranges never depend on the order streams are listed in.
    state.visibleStreamIds = {"imu_az", "imu_ax", "imu_ay"};
    const PlotSpec reordered = buildPlotSpec(log.info, log.streams, {}, state);
    CHECK(axisOf(reordered, "imu_ax").lower == padded.lower);
    CHECK(traceIndex(reordered, "imu_az") == 0);
}

void checkCompassTag()
{
    Fixture log = loadFixture("compasstag.db3");
    CHECK(log.info.timeDomain == sensordata::TimeDomain::EpochSeconds);
    CHECK(find(log, "activity") != nullptr && find(log, "voltage") != nullptr);

    PlotState state;
    state.utcOffsetHours = -5;
    state.declinationDegrees = 12.5;
    state.visibleStreamIds = {"activity", "voltage"};
    PlotSpec spec = buildPlotSpec(log.info, log.streams, {}, state);
    CHECK(spec.timeDomain == sensordata::TimeDomain::EpochSeconds && spec.utcOffsetHours == -5);
    if (find(log, "activity")) {
        const PlotAxisSpec activity = axisOf(spec, "activity");
        CHECK(activity.mode == PlotAxisRangeMode::Fixed);
        CHECK(activity.lower == 0.0 && activity.upper == 100.0);
    }
    if (find(log, "voltage")) {
        const PlotAxisSpec voltage = axisOf(spec, "voltage");
        CHECK(voltage.mode == PlotAxisRangeMode::Fixed && voltage.upper == 5.0);
        CHECK(voltage.side == AxisSide::Right);
    }
    // Epoch logs never show the start row.
    CHECK(spec.metadataRows.empty());

    // A custom range wins over a fixed default.
    state.customRanges["activity"] = PlotRange{10.0, 20.0};
    spec = buildPlotSpec(log.info, log.streams, {}, state);
    if (find(log, "activity")) {
        CHECK(axisOf(spec, "activity").lower == 10.0 && axisOf(spec, "activity").upper == 20.0);
    }

    // Heading adds the declination row.
    addDerivedCopy(log, log.streams.front().id, "compass_heading");
    state.visibleStreamIds = {"compass_heading"};
    spec = buildPlotSpec(log.info, log.streams, {}, state);
    CHECK(spec.metadataRows == std::vector<std::string>{"Decl:    12.50 deg"});
}

/// The collection-start format: UTC, rounded to the nearest second.
void checkFormatting()
{
    CHECK(sensorviz::formatCollectionStart(0) == "01/01/70 00:00:00 UTC");
    CHECK(sensorviz::formatCollectionStart(1499) == "01/01/70 00:00:01 UTC");
    CHECK(sensorviz::formatCollectionStart(1500) == "01/01/70 00:00:02 UTC");
    CHECK(sensorviz::formatCollectionStart(kCollectionStartMs) == "08/27/26 12:35:47 UTC");
    CHECK(sensorviz::paddedRange(PlotRange{0.0, 10.0}) == (PlotRange{-0.5, 10.5}));
    CHECK(sensorviz::paddedRange(PlotRange{100.0, 100.0}) == (PlotRange{94.5, 105.5}));
}

} // namespace

/**
 * @brief   Runs every check.
 * @return  0 when all pass, 1 otherwise.
 */
int main()
{
    checkImuTag();
    checkCompassTag();
    checkFormatting();
    std::printf("plotspec_check: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
