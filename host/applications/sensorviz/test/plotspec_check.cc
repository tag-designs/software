/**
 * @file    plotspec_check.cc
 * @brief   Offline assertion checks for buildPlotSpec(), sensorViz's plot policy.
 *
 * @details Loads the committed sensorviz fixtures with the real SQLite loader
 *          and checks the PlotSpec built for a range of plot states, with no
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
#include "sqlite_loader.h"

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

/// Loads a fixture, failing the program if it does not load.
SensorLog loadFixture(const char *name)
{
    SensorLog log;
    QString error;
    const QString path =
        QString(TAG_DESIGNS_SOURCE_DIR) + "/host/docs/fixtures/sensorviz/" + name;
    if (!SqliteLoader::load(path, log, error)) {
        std::fprintf(stderr, "cannot load %s: %s\n", name, qPrintable(error));
        std::exit(2);
    }
    return log;
}

/// The stream with @p id, or null.
const SensorStream *find(const SensorLog &log, const QString &id)
{
    for (const SensorStream &stream : log.streams) {
        if (stream.id == id) {
            return &stream;
        }
    }
    return nullptr;
}

/// Whole-stream range from the sensordata summary, as the spec must use.
PlotRange infoRange(const SensorLog &log, const QString &id)
{
    const sensordata::StreamInfo *info = log.data->info(find(log, id)->dataId);
    return PlotRange{info->min, info->max};
}

/// Index of the trace for @p id, or -1.
int traceIndex(const PlotSpec &spec, const QString &id)
{
    for (int i = 0; i < spec.traces.size(); i++) {
        if (spec.traces[i].id == id) {
            return i;
        }
    }
    return -1;
}

/// The axis of the trace for @p id; the check fails, and a default is returned, when absent.
PlotAxisSpec axisOf(const PlotSpec &spec, const QString &id)
{
    const int index = traceIndex(spec, id);
    CHECK(index >= 0);
    return index >= 0 ? spec.axes[spec.traces[index].axis] : PlotAxisSpec{};
}

/// A derived copy of stream @p from registered as @p id, as a transform would make it.
void addDerivedCopy(SensorLog &log, const QString &from, const QString &id)
{
    SensorStream stream = *find(log, from);
    sensordata::StreamInfo info;
    info.id = id.toStdString();
    stream.id = id;
    stream.derived = false;
    stream.dataId = *log.data->addComputed(
        info, {stream.dataId}, [](const float *values) { return static_cast<double>(values[0]); });
    log.streams.append(stream);
}

void checkImuTag()
{
    SensorLog log = loadFixture("imutag.db3");
    CHECK(log.timeDomain == SensorTimeDomain::ElapsedSeconds);
    CHECK(log.hasCollectionStart);

    PlotState state;
    state.title = "imutag.db3";
    state.visibleStreamIds = {"imu_ax", "imu_ay", "imu_az", "imu_pressure", "no_such_stream"};
    PlotSpec spec = buildPlotSpec(log, log.streams, {}, state);

    // Traces in state order, unknown ids ignored, one axis each.
    CHECK(spec.traces.size() == 4 && spec.axes.size() == 4);
    CHECK(traceIndex(spec, "imu_ax") == 0 && traceIndex(spec, "imu_pressure") == 3);
    for (int i = 0; i < spec.traces.size(); i++) {
        CHECK(spec.traces[i].axis == i);
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
        CHECK(axis.side == SensorAxisSide::Right);
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
    int marker_count = 0;
    for (const SensorEventMarker &marker : log.eventMarkers) {
        marker_count += marker.time.size();
    }
    CHECK(spec.eventMarkerTimes.size() == marker_count);
    CHECK(spec.metadataRows.size() == 1 && spec.metadataRows[0].startsWith("Start:   "));
    CHECK(spec.metadataRows[0].endsWith(" UTC"));
    CHECK(spec.title == "imutag.db3");
    CHECK(spec.timeDomain == SensorTimeDomain::ElapsedSeconds);

    // A custom range fixes one member; the other two stay linked to each other.
    state.customRanges.insert("imu_ay", PlotRange{-5.0, 5.0});
    spec = buildPlotSpec(log, log.streams, {}, state);
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
    spec = buildPlotSpec(log, log.streams, {}, state);
    CHECK(axisOf(spec, "imu_gx").mode == PlotAxisRangeMode::Auto);
    CHECK(axisOf(spec, "imu_ax").mode == PlotAxisRangeMode::Auto);

    // Preferences override colour and side.
    PlotDisplayPreferences preferences;
    preferences.colors.insert("imu_ax", QColor(1, 2, 3));
    preferences.axisSides.insert("imu_ax", SensorAxisSide::Left);
    spec = buildPlotSpec(log, log.streams, preferences, state);
    CHECK(axisOf(spec, "imu_ax").color == QColor(1, 2, 3));
    CHECK(spec.traces[traceIndex(spec, "imu_ax")].color == QColor(1, 2, 3));
    CHECK(axisOf(spec, "imu_ax").side == SensorAxisSide::Left);
    CHECK(axisOf(spec, "imu_gx").side == SensorAxisSide::Right);

    // Altitude adds the sea-level row; derived streams draw heavier.
    addDerivedCopy(log, "imu_pressure", "altitude");
    log.streams.last().derived = true;
    state.visibleStreamIds = {"altitude"};
    state.seaLevelPressure = 1000.5;
    state.titleVisible = false;
    spec = buildPlotSpec(log, log.streams, {}, state);
    CHECK(spec.traces.size() == 1 && spec.traces[0].penWidth == 2);
    CHECK(spec.metadataRows.size() == 2 && spec.metadataRows[1] == "MSL:     1000.50 mbar");
    CHECK(spec.title.isEmpty());

    // Nothing visible: no traces, extent or markers; the start row remains.
    state.visibleStreamIds.clear();
    spec = buildPlotSpec(log, log.streams, {}, state);
    CHECK(spec.traces.isEmpty() && spec.axes.isEmpty() && !spec.hasXDataRange);
    CHECK(spec.eventMarkerTimes.isEmpty());
    CHECK(spec.metadataRows.size() == 1);

    // Linked ranges never depend on the order streams are listed in.
    state.visibleStreamIds = {"imu_az", "imu_ax", "imu_ay"};
    const PlotSpec reordered = buildPlotSpec(log, log.streams, {}, state);
    CHECK(axisOf(reordered, "imu_ax").lower == padded.lower);
    CHECK(traceIndex(reordered, "imu_az") == 0);
}

void checkCompassTag()
{
    SensorLog log = loadFixture("compasstag.db3");
    CHECK(log.timeDomain == SensorTimeDomain::EpochSeconds);
    CHECK(find(log, "activity") != nullptr && find(log, "voltage") != nullptr);

    PlotState state;
    state.utcOffsetHours = -5;
    state.declinationDegrees = 12.5;
    state.visibleStreamIds = {"activity", "voltage"};
    PlotSpec spec = buildPlotSpec(log, log.streams, {}, state);
    CHECK(spec.timeDomain == SensorTimeDomain::EpochSeconds && spec.utcOffsetHours == -5);
    if (find(log, "activity")) {
        const PlotAxisSpec activity = axisOf(spec, "activity");
        CHECK(activity.mode == PlotAxisRangeMode::Fixed);
        CHECK(activity.lower == 0.0 && activity.upper == 100.0);
    }
    if (find(log, "voltage")) {
        const PlotAxisSpec voltage = axisOf(spec, "voltage");
        CHECK(voltage.mode == PlotAxisRangeMode::Fixed && voltage.upper == 5.0);
        CHECK(voltage.side == SensorAxisSide::Right);
    }
    // Epoch logs never show the start row.
    CHECK(spec.metadataRows.isEmpty());

    // A custom range wins over a fixed default.
    state.customRanges.insert("activity", PlotRange{10.0, 20.0});
    spec = buildPlotSpec(log, log.streams, {}, state);
    if (find(log, "activity")) {
        CHECK(axisOf(spec, "activity").lower == 10.0 && axisOf(spec, "activity").upper == 20.0);
    }

    // Heading adds the declination row.
    addDerivedCopy(log, log.streams.first().id, "compass_heading");
    state.visibleStreamIds = {"compass_heading"};
    spec = buildPlotSpec(log, log.streams, {}, state);
    CHECK(spec.metadataRows == QStringList{"Decl:    12.50 deg"});
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
    std::printf("plotspec_check: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
