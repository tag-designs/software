/**
 * @file    transforms_check.cc
 * @brief   Offline assertion checks for sensorViz's display transforms
 *          (TransformRegistry), without Qt.
 *
 * @details Opens the committed fixtures with openLog(), sets up a ViewModel as
 *          sensorViz does after a load, and runs the registry through a sink
 *          that records each output as the Qt app would add it. Checks which
 *          transforms apply, the outputs and their order and visibility, the
 *          parameter specs, that declination and battery direction remake
 *          only the heading (and by how much), that sea-level pressure remakes
 *          altitude and carries a pressure range over, and the values of the
 *          magnitudes and compass samples against the inputs and
 *          sensoranalysis's core.
 *
 *          The outputs' exact equality with the Qt code they replaced was
 *          checked with sensorViz's `--dump-state` when they moved; see
 *          ../test/README.md.
 *
 *          Exits 0 when every check passes, 1 otherwise; each failure is
 *          printed with its line.
 */
#include "transforms.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

using sensorviz::StreamCatalog;
using sensorviz::StreamMeta;
using sensorviz::TransformContext;
using sensorviz::TransformRegistry;
using sensorviz::ViewModel;

int g_failures = 0; ///< Checks failed so far.
int g_checks = 0;   ///< Checks run so far.

/// Records one check; prints the failing expression and line.
#define CHECK(condition)                                                              \
    do {                                                                              \
        g_checks++;                                                                   \
        if (!(condition)) {                                                           \
            g_failures++;                                                             \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                             \
    } while (0)

/**
 * @class   RecordingSink
 * @brief   Adds outputs to the ViewModel as the Qt app does, and records them.
 */
class RecordingSink : public sensorviz::TransformSink
{
public:
    /// A sink over @p view and @p catalog's log, which it frees replaced data from.
    RecordingSink(ViewModel &view, const StreamCatalog &catalog) : view_(view), catalog_(catalog) {}

    void publish(const StreamMeta &stream, bool visible) override
    {
        published.push_back(stream.id + (visible ? "+" : "-"));
        const ViewModel::StreamUpdate update = view_.addOrReplaceStream(stream, visible);
        if (update.released != sensordata::kNoStream) {
            catalog_.data->remove(update.released);
        }
    }
    void warn(const std::string &message) override { warnings.push_back(message); }

    std::vector<std::string> published; ///< Output ids in order, `+` shown, `-` hidden.
    std::vector<std::string> warnings;  ///< Warnings received.

private:
    ViewModel &view_;              ///< Where outputs go.
    const StreamCatalog &catalog_; ///< The log, for releasing replaced data.
};

/// The committed fixture log @p name.
std::string fixture(const char *name)
{
    return std::string(TAG_DESIGNS_SOURCE_DIR) + "/host/docs/fixtures/sensorviz/" + name;
}

/// Every sample value of @p id as the Log holds it, in order.
std::vector<float> values(const StreamCatalog &catalog, const ViewModel &view, const char *id)
{
    const StreamMeta *stream = view.stream(id);
    if (!stream) {
        return {};
    }
    const sensordata::StreamInfo *info = catalog.data->info(stream->dataId);
    const sensordata::Envelope envelope = catalog.data->view(
        stream->dataId, -INFINITY, INFINITY, static_cast<int>(info->samples), sensordata::GapMode::Join);
    std::vector<float> out(info->samples, NAN);
    for (size_t i = 0; i < envelope.index.size(); i++) {
        out[envelope.index[i]] = envelope.value[i];
    }
    return out;
}

/// IMUTag: altitude and magnitudes; no compass (ImuAccel and ImuMag are never paired).
void checkImuTag()
{
    StreamCatalog catalog;
    std::string error;
    CHECK(sensorviz::openLog(fixture("imutag.db3"), catalog, error));
    ViewModel view;
    view.resetForLog(catalog.streams, "imutag.db3");
    RecordingSink sink(view, catalog);
    TransformContext context(catalog, view, sink);
    TransformRegistry registry = TransformRegistry::makeDefault();

    CHECK(registry.applicable("altitude", catalog, view));
    CHECK(registry.applicable("imu_magnitudes", catalog, view));
    CHECK(!registry.applicable("compass", catalog, view));
    CHECK(registry.runAll(context).empty());
    CHECK((sink.published == std::vector<std::string>{"altitude+", "imu_accel_magnitude+",
                                                      "imu_gyro_magnitude+", "imu_mag_magnitude+"}));
    CHECK(sink.warnings.empty());
    CHECK(registry.compass()->samples().empty());

    // Each magnitude is the length of its three stored axes.
    const std::vector<float> ax = values(catalog, view, "imu_ax");
    const std::vector<float> ay = values(catalog, view, "imu_ay");
    const std::vector<float> az = values(catalog, view, "imu_az");
    const std::vector<float> magnitude = values(catalog, view, "imu_accel_magnitude");
    CHECK(!magnitude.empty() && magnitude.size() == ax.size());
    size_t wrong = 0;
    for (size_t i = 0; i < magnitude.size() && i < ax.size(); i++) {
        const double x = ax[i], y = ay[i], z = az[i];
        if (magnitude[i] != static_cast<float>(std::sqrt(x * x + y * y + z * z))) {
            wrong++;
        }
    }
    CHECK(wrong == 0);
    CHECK(view.stream("imu_accel_magnitude")->group == view.stream("imu_ax")->group);

    // Sea-level pressure remakes altitude only, and a pressure range carries over.
    const std::vector<float> before = values(catalog, view, "altitude");
    view.setCustomRange("imu_pressure", sensorviz::PlotRange{950.0, 1010.0}, true);
    sink.published.clear();
    CHECK(registry.setParameter(context, ViewModel::kSeaLevelPressure, 1000.0, error));
    CHECK(view.seaLevelPressure() == 1000.0);
    CHECK((sink.published == std::vector<std::string>{"altitude+"}));
    const std::vector<float> after = values(catalog, view, "altitude");
    CHECK(after.size() == before.size() && after != before);
    CHECK(view.hasCustomRange("altitude") && !view.isExplicitRange("altitude"));
    CHECK(view.customRanges().at("altitude").lower == sensorviz::altitudeFromPressure(1010.0, 1000.0));

    // The compass parameters change nothing here.
    sink.published.clear();
    CHECK(registry.setParameter(context, ViewModel::kDeclination, 12.5, error));
    CHECK(sink.published.empty());
    CHECK(view.declinationDegrees() == 12.5);
}

/// CompassTag: the compass transform from raw rows, and its parameters.
void checkCompassTag()
{
    StreamCatalog catalog;
    std::string error;
    CHECK(sensorviz::openLog(fixture("compasstag.db3"), catalog, error));
    ViewModel view;
    view.resetForLog(catalog.streams, "compasstag.db3");
    RecordingSink sink(view, catalog);
    TransformContext context(catalog, view, sink);
    TransformRegistry registry = TransformRegistry::makeDefault();

    CHECK(registry.applicable("compass", catalog, view));
    CHECK(!registry.applicable("altitude", catalog, view));
    CHECK(registry.runAll(context).empty());
    CHECK((sink.published == std::vector<std::string>{
               "compass_heading+", "compass_acceleration+", "compass_pitch-", "compass_roll-",
               "compass_dip-", "compass_field-"}));

    const sensorviz::CompassTransform &compass = *registry.compass();
    CHECK(compass.source() == "derived from raw compass samples");
    CHECK(compass.samples().size() == 44);

    // Each sample is the core's solve of its raw row.
    const sensorviz::RecordSet *raw = nullptr;
    for (const sensorviz::RecordSet &set : catalog.recordSets) {
        if (set.id == "compass_raw") {
            raw = &set;
        }
    }
    CHECK(raw != nullptr);
    size_t row = 0;
    size_t wrong = 0;
    catalog.data->forEachRow(
        raw->table, {"ax", "ay", "az", "mx", "my", "mz"},
        [&](double time, const double *v) {
            const sensormath::Vec3f accel = compasscore::applyAccelerometer(
                catalog.compassCalibration,
                sensormath::Vec3f{float(v[0]), float(v[1]), float(v[2])});
            compasscore::Orientation expected;
            compasscore::solveOrientation(sensormath::Vec3f{float(v[3]), float(v[4]), float(v[5])},
                                          accel, &catalog.compassCalibration, expected);
            const sensorviz::OrientationSample &got = compass.samples()[row++];
            if (got.time != time || got.q.w != expected.q.w || got.q.z != expected.q.z
                || got.dip != expected.dip || got.mg != expected.mg) {
                wrong++;
            }
        },
        error);
    CHECK(row == 44 && wrong == 0);

    // Heading is the yaw; pitch is the core's display angle.
    const std::vector<float> heading = values(catalog, view, "compass_heading");
    const std::vector<float> pitch = values(catalog, view, "compass_pitch");
    const compasscore::DisplayAngles first = compasscore::displayAngles(compass.samples()[0].q);
    CHECK(heading[0] == static_cast<float>(std::fmod(720.0 + first.yaw, 360.0)));
    CHECK(pitch[0] == static_cast<float>(first.pitch));

    // Declination remakes only the heading, keeping its visibility.
    view.setStreamVisible("compass_heading", false);
    sink.published.clear();
    CHECK(registry.setParameter(context, ViewModel::kDeclination, 10.0, error));
    CHECK((sink.published == std::vector<std::string>{"compass_heading-"}));
    const std::vector<float> declined = values(catalog, view, "compass_heading");
    CHECK(declined[0] == static_cast<float>(std::fmod(720.0 + first.yaw + 10.0, 360.0)));

    // Battery aft turns the heading round.
    sink.published.clear();
    CHECK(registry.setParameter(context, ViewModel::kBatteryForward, 0.0, error));
    CHECK(!view.batteryForward());
    CHECK((sink.published == std::vector<std::string>{"compass_heading-"}));
    const double forward = std::fmod(720.0 + first.yaw + 10.0, 360.0);
    CHECK(values(catalog, view, "compass_heading")[0]
          == static_cast<float>(std::fmod(720.0 + forward + 180.0, 360.0)));

    // A new run starts again from the log.
    sink.published.clear();
    CHECK(registry.runAll(context).empty());
    CHECK(sink.published.size() == 6);
    CHECK(registry.compass()->samples().size() == 44);
}

/// The parameter specs the Qt dialog and a web form are built from.
void checkParameters()
{
    const TransformRegistry registry = TransformRegistry::makeDefault();
    const sensorviz::ParameterSpec *seaLevel = registry.parameter(ViewModel::kSeaLevelPressure);
    CHECK(seaLevel && seaLevel->kind == sensorviz::ParameterSpec::Kind::Number);
    CHECK(seaLevel && seaLevel->title == "Sea-level Pressure"
          && seaLevel->prompt == "Mean sea-level pressure (mbar)" && seaLevel->minimum == 800.0
          && seaLevel->maximum == 1200.0 && seaLevel->decimals == 2);
    const sensorviz::ParameterSpec *declination = registry.parameter(ViewModel::kDeclination);
    CHECK(declination && declination->minimum == -180.0 && declination->maximum == 180.0
          && declination->decimals == 2 && declination->title == "Declination");
    const sensorviz::ParameterSpec *battery = registry.parameter(ViewModel::kBatteryForward);
    CHECK(battery && battery->kind == sensorviz::ParameterSpec::Kind::Toggle);
    CHECK(registry.parameter(ViewModel::kActivityTimeConstant) == nullptr);
    CHECK(registry.parameter("nonsense") == nullptr);

    ViewModel view;
    CHECK(view.parameter(ViewModel::kSeaLevelPressure) == ViewModel::kDefaultSeaLevelPressure);
    CHECK(view.parameter(ViewModel::kBatteryForward) == 1.0);
    CHECK(view.setParameter(ViewModel::kBatteryForward, 0.0) == ViewModel::Parameters);
    CHECK(!view.batteryForward());
    CHECK(view.setParameter("nonsense", 1.0) == ViewModel::None);
    CHECK(std::isnan(view.parameter("nonsense")));
}

} // namespace

/// Runs every check; exits 0 when all pass, 1 otherwise.
int main()
{
    checkImuTag();
    checkCompassTag();
    checkParameters();
    std::printf("transforms_check: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
