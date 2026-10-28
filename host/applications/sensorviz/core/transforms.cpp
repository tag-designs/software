/**
 * @file    transforms.cpp
 * @brief   sensorViz's display transforms and their registry, without Qt.
 *
 * @details Moved from the Qt front end's `transforms.cpp` and
 *          `compass_transforms.cpp`, step for step, so that every output is
 *          bit-identical to what those produced (checked with sensorViz's
 *          `--dump-state`). The order of outputs within a transform is kept
 *          too: the Qt app redraws after each, and its y axes take their range
 *          partly from the previous drawing.
 */
#include "transforms.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace sensorviz {
namespace {

/// Stored orientation record sets from `dataprocessing`, in order of
/// preference: a processed CompassTag log also has `compass_raw`, and the
/// stored quaternions are what the file says.
const char *const kStoredOrientationRecordSets[] = {
    "compass_orientation",
    "imu_compass_orientation",
};

/// The record set @p id of the log, or null.
const RecordSet *recordSet(const StreamCatalog &catalog, const char *id)
{
    for (const RecordSet &set : catalog.recordSets) {
        if (set.id == id) {
            return &set;
        }
    }
    return nullptr;
}

/// The first stored orientation record set, or null.
const RecordSet *storedOrientation(const StreamCatalog &catalog)
{
    for (const char *id : kStoredOrientationRecordSets) {
        if (const RecordSet *set = recordSet(catalog, id)) {
            return set;
        }
    }
    return nullptr;
}

/// True when @p set has every one of @p columns.
bool hasColumns(const RecordSet &set, const std::vector<std::string> &columns)
{
    for (const std::string &column : columns) {
        if (std::find(set.columns.begin(), set.columns.end(), column) == set.columns.end()) {
            return false;
        }
    }
    return true;
}

/// The nearest sample of (@p time, @p values) to @p at; on a tie, the earlier.
bool nearestValue(const std::vector<double> &time, const std::vector<double> &values, double at,
                  double &value)
{
    if (time.empty() || values.size() != time.size()) {
        return false;
    }
    const auto it = std::lower_bound(time.cbegin(), time.cend(), at);
    size_t index = 0;
    if (it == time.cend()) {
        index = time.size() - 1;
    } else if (it == time.cbegin()) {
        index = 0;
    } else {
        const size_t after = static_cast<size_t>(std::distance(time.cbegin(), it));
        const size_t before = after - 1;
        index = std::abs(time[after] - at) < std::abs(at - time[before]) ? after : before;
    }
    value = values[index];
    return true;
}

/// Heading in [0, 360) from magnetic yaw plus @p declination.
double displayHeadingFromYaw(double yaw, double declination)
{
    return std::fmod(720.0 + yaw + declination, 360.0);
}

/**
 * @class   AltitudeTransform
 * @brief   `altitude` from `pressure` or `imu_pressure`, temperature-corrected
 *          by `sensor_temperature` when present.
 */
class AltitudeTransform : public Transform
{
public:
    const char *id() const override { return "altitude"; }

    bool applicable(const StreamCatalog &, const ViewModel &view) const override
    {
        return view.hasStream("pressure") || view.hasStream("imu_pressure");
    }

    std::vector<ParameterSpec> parameters() const override
    {
        ParameterSpec seaLevel;
        seaLevel.id = ViewModel::kSeaLevelPressure;
        seaLevel.title = "Sea-level Pressure";
        seaLevel.prompt = "Mean sea-level pressure (mbar)";
        seaLevel.units = "mbar";
        seaLevel.minimum = 800.0;
        seaLevel.maximum = 1200.0;
        seaLevel.decimals = 2;
        return {seaLevel};
    }

    bool run(TransformContext &context, std::string &) override
    {
        ViewModel &view = context.view();
        const StreamMeta *pressure = view.stream("pressure");
        if (!pressure) {
            pressure = view.stream("imu_pressure");
        }
        if (!pressure || !context.catalog().data) {
            return true;
        }
        const StreamMeta pressureMeta = *pressure;
        const bool visible = context.visibleOnRemake("altitude");

        StreamMeta altitude;
        altitude.id = "altitude";
        altitude.label = "Altitude";
        altitude.units = "m";
        altitude.timeDomain = pressureMeta.timeDomain;
        altitude.color = Rgb{110, 70, 180};
        altitude.defaultVisible = true;
        altitude.axisSide = AxisSide::Left;

        // The SQLite doubles, so altitude is exactly what it was computed
        // from; pressure is logged at a low rate.
        std::vector<double> pressureTime;
        std::vector<double> pressureMbar;
        if (!context.readDoubles(pressureMeta, pressureTime, pressureMbar)) {
            return true;
        }
        std::vector<double> temperatureTime;
        std::vector<double> temperatureC;
        const StreamMeta *temperature = view.stream("sensor_temperature");
        const bool haveTemperature = temperature
            && temperature->timeDomain == pressureMeta.timeDomain
            && context.readDoubles(*temperature, temperatureTime, temperatureC);

        const double seaLevel = view.seaLevelPressure();
        std::vector<double> values;
        values.reserve(pressureMbar.size());
        for (size_t i = 0; i < pressureMbar.size(); i++) {
            double celsius = 0.0;
            if (haveTemperature && nearestValue(temperatureTime, temperatureC, pressureTime[i], celsius)) {
                values.push_back(altitudeFromPressure(pressureMbar[i], celsius, seaLevel));
            } else {
                values.push_back(altitudeFromPressure(pressureMbar[i], seaLevel));
            }
        }
        const std::optional<sensordata::StreamId> data = context.catalog().data->addDerived(
            derivedStreamInfo(altitude), pressureMeta.dataId, values);
        if (!data) {
            return true;
        }
        altitude.dataId = *data;

        // A pressure range the user set carries over, unless altitude has its own.
        if (view.hasCustomRange(pressureMeta.id) && !view.isExplicitRange("altitude")) {
            const PlotRange range = view.customRanges().at(pressureMeta.id);
            view.setCustomRange(
                "altitude",
                PlotRange{altitudeFromPressure(range.upper, seaLevel),
                          altitudeFromPressure(range.lower, seaLevel)},
                false);
        }
        context.sink().publish(altitude, visible);
        return true;
    }
};

/**
 * @class   ImuMagnitudeTransform
 * @brief   Acceleration, gyro and magnetic field magnitudes from IMUTag's axes.
 *
 * @details Computed from the stored floats (the calibrated axes, where
 *          calibration applies); a magnitude whose three axes are not samples
 *          of one table is not made.
 */
class ImuMagnitudeTransform : public Transform
{
public:
    const char *id() const override { return "imu_magnitudes"; }

    bool applicable(const StreamCatalog &, const ViewModel &view) const override
    {
        for (const Magnitude &magnitude : kMagnitudes) {
            if (view.hasStream(magnitude.axes[0]) && view.hasStream(magnitude.axes[1])
                && view.hasStream(magnitude.axes[2])) {
                return true;
            }
        }
        return false;
    }

    bool run(TransformContext &context, std::string &) override
    {
        const ViewModel &view = context.view();
        sensordata::Log *log = context.catalog().data.get();
        for (const Magnitude &magnitude : kMagnitudes) {
            const StreamMeta *x = view.stream(magnitude.axes[0]);
            const StreamMeta *y = view.stream(magnitude.axes[1]);
            const StreamMeta *z = view.stream(magnitude.axes[2]);
            if (!x || !y || !z || !log) {
                continue;
            }

            StreamMeta stream;
            stream.id = magnitude.id;
            stream.label = magnitude.label;
            stream.units = magnitude.units;
            stream.timeDomain = x->timeDomain;
            // A magnitude shares its components' axis, so it joins their group.
            stream.group = x->group;
            stream.groupLabel = x->groupLabel;
            stream.color = defaultDisplayForStream(magnitude.id).color;
            stream.defaultVisible = true;
            stream.axisSide = AxisSide::Left;
            stream.axisRange = magnitude.range;

            const std::optional<sensordata::StreamId> data = log->addComputed(
                derivedStreamInfo(stream), {x->dataId, y->dataId, z->dataId},
                [](const float *v) {
                    const double vx = v[0];
                    const double vy = v[1];
                    const double vz = v[2];
                    return std::sqrt(vx * vx + vy * vy + vz * vz);
                });
            if (!data) {
                continue;
            }
            stream.dataId = *data;
            context.sink().publish(stream, context.visibleOnRemake(stream.id));
        }
        return true;
    }

private:
    /// One magnitude: its output and the three axes it is made from.
    struct Magnitude
    {
        const char *id;      ///< Output stream id.
        const char *label;   ///< Output label.
        const char *units;   ///< Output units.
        AxisRange range;     ///< Default fixed range.
        const char *axes[3]; ///< Input stream ids.
    };
    /// The magnitudes, in output order.
    static constexpr Magnitude kMagnitudes[] = {
        {"imu_accel_magnitude", "Acceleration magnitude", "mg", {true, 0.0, 2000.0},
         {"imu_ax", "imu_ay", "imu_az"}},
        {"imu_gyro_magnitude", "Gyro magnitude", "dps", {}, {"imu_gx", "imu_gy", "imu_gz"}},
        {"imu_mag_magnitude", "Magnetic field magnitude", "uT", {}, {"imu_mx", "imu_my", "imu_mz"}},
    };
};

/// A compass output's metadata; the caller registers its values.
StreamMeta compassStream(const char *id, const char *label, const char *units, Rgb color,
                         bool defaultVisible, AxisRange range = {})
{
    StreamMeta stream;
    stream.id = id;
    stream.label = label;
    stream.units = units;
    stream.color = color;
    stream.defaultVisible = defaultVisible;
    stream.axisRange = range;
    return stream;
}

/// The heading output's metadata.
StreamMeta headingStream()
{
    return compassStream("compass_heading", "Heading", "deg", Rgb{195, 40, 175}, true,
                         {true, 0.0, 360.0});
}

/// Registers @p values on @p timeSource's axis as @p stream's data and publishes it.
void publishCompass(TransformContext &context, sensordata::StreamId timeSource, StreamMeta stream,
                    const std::vector<double> &values, bool visible)
{
    sensordata::Log *log = context.catalog().data.get();
    if (!log) {
        return;
    }
    stream.timeDomain = context.catalog().timeDomain;
    const std::optional<sensordata::StreamId> data =
        log->addDerived(derivedStreamInfo(stream), timeSource, values.data(), values.size());
    if (!data) {
        context.sink().warn("Could not register compass stream " + stream.id);
        return;
    }
    stream.dataId = *data;
    context.sink().publish(stream, visible);
}

} // namespace

bool TransformContext::visibleOnRemake(const std::string &id) const
{
    const StreamMeta *stream = view_.stream(id);
    return !stream || stream->derived || view_.isVisible(stream->id);
}

bool TransformContext::readDoubles(const StreamMeta &stream, std::vector<double> &time,
                                   std::vector<double> &value) const
{
    const sensordata::Log *log = catalog_.data.get();
    const sensordata::StreamInfo *info = log ? log->info(stream.dataId) : nullptr;
    if (!info || info->derived || info->value_column.empty()) {
        return false;
    }
    time.clear();
    value.clear();
    time.reserve(info->samples);
    value.reserve(info->samples);
    std::string error;
    const bool ok = log->forEachRow(
        info->table, {info->value_column},
        [&time, &value](double t, const double *values) {
            time.push_back(t);
            value.push_back(values[0]);
        },
        error);
    if (!ok) {
        sink_.warn("Could not read " + stream.id + " : " + error);
    }
    return ok;
}

sensordata::StreamInfo derivedStreamInfo(const StreamMeta &stream)
{
    sensordata::StreamInfo info;
    info.id = stream.id;
    info.label = stream.label;
    info.units = stream.units;
    info.kind = "derived";
    info.domain = stream.timeDomain;
    return info;
}

double altitudeFromPressure(double pressureMbar, double seaLevelMbar)
{
    return 44330.0 * (1.0 - std::pow(pressureMbar / seaLevelMbar, 1.0 / 5.257));
}

double altitudeFromPressure(double pressureMbar, double temperatureC, double seaLevelMbar)
{
    const double temperatureK = temperatureC + 273.15;
    return (std::pow(seaLevelMbar / pressureMbar, 1.0 / 5.257) - 1.0) * temperatureK / 0.0065;
}

// --- CompassTransform ------------------------------------------------------

bool CompassTransform::applicable(const StreamCatalog &catalog, const ViewModel &) const
{
    return storedOrientation(catalog) != nullptr
        || (catalog.hasCompassCalibration && recordSet(catalog, "compass_raw") != nullptr);
}

std::vector<ParameterSpec> CompassTransform::parameters() const
{
    ParameterSpec declination;
    declination.id = ViewModel::kDeclination;
    declination.title = "Declination";
    declination.prompt = "Declination angle (degrees)";
    declination.units = "deg";
    declination.minimum = -180.0;
    declination.maximum = 180.0;
    declination.decimals = 2;

    ParameterSpec battery;
    battery.id = ViewModel::kBatteryForward;
    battery.title = "Battery Forward";
    battery.prompt = "Battery forward";
    battery.kind = ParameterSpec::Kind::Toggle;
    return {declination, battery};
}

void CompassTransform::reset()
{
    samples_.clear();
    source_.clear();
    timeSource_ = sensordata::kNoStream;
}

bool CompassTransform::loadSamples(const TransformContext &context, std::string &error)
{
    reset();
    const StreamCatalog &catalog = context.catalog();
    if (!catalog.data) {
        error = "The log has no sample data.";
        return false;
    }
    std::string readError;

    if (const RecordSet *stored = storedOrientation(catalog)) {
        const std::vector<std::string> columns = {"qw", "qx", "qy", "qz", "dip", "field", "acceleration"};
        if (!hasColumns(*stored, columns)) {
            error = "The " + stored->label + " record set is missing one or more expected columns.";
            return false;
        }
        const bool read = catalog.data->forEachRow(
            stored->table, columns,
            [this](double time, const double *v) {
                OrientationSample sample;
                sample.time = time;
                sample.q = sensormath::Quatf{static_cast<float>(v[0]), static_cast<float>(v[1]),
                                             static_cast<float>(v[2]), static_cast<float>(v[3])};
                sample.dip = static_cast<float>(v[4]);
                sample.field = static_cast<float>(v[5]);
                sample.mg = static_cast<float>(v[6]);
                samples_.push_back(sample);
            },
            readError);
        if (!read) {
            samples_.clear();
            error = readError;
            return false;
        }
        timeSource_ = stored->timeSource;
        source_ = "stored quaternions (" + stored->label + ")";
        return true;
    }

    // Live orientation comes only from CompassTag's compass_raw, whose
    // accelerometer and magnetometer samples are taken together.
    const RecordSet *raw = recordSet(catalog, "compass_raw");
    if (!raw || !catalog.hasCompassCalibration) {
        error = "Compass orientation, or compass raw samples and calibration constants, are required.";
        return false;
    }
    const std::vector<std::string> columns = {"ax", "ay", "az", "mx", "my", "mz"};
    if (!hasColumns(*raw, columns)) {
        error = "The compass raw record set is missing one or more expected columns.";
        return false;
    }
    const compasscore::CalibrationConstants constants = catalog.compassCalibration;
    const bool read = catalog.data->forEachRow(
        raw->table, columns,
        [this, &constants](double time, const double *v) {
            // As CompassProcessor::deriveSample() did: the accelerometer
            // offset removed, the magnetometer calibrated inside the solve;
            // with no solution, the identity and zeros.
            const sensormath::Vec3f accel = compasscore::applyAccelerometer(
                constants, sensormath::Vec3f{static_cast<float>(v[0]), static_cast<float>(v[1]),
                                             static_cast<float>(v[2])});
            const sensormath::Vec3f mag{static_cast<float>(v[3]), static_cast<float>(v[4]),
                                        static_cast<float>(v[5])};
            OrientationSample sample;
            sample.time = time;
            compasscore::Orientation orientation;
            if (compasscore::solveOrientation(mag, accel, &constants, orientation)) {
                sample.q = orientation.q;
                sample.dip = orientation.dip;
                sample.field = orientation.field;
                sample.mg = orientation.mg;
            }
            samples_.push_back(sample);
        },
        readError);
    if (!read) {
        samples_.clear();
        error = readError;
        return false;
    }
    timeSource_ = raw->timeSource;
    source_ = "derived from raw compass samples";
    return true;
}

void CompassTransform::publishHeading(TransformContext &context, bool visible) const
{
    const double declination = context.view().declinationDegrees();
    const bool batteryForward = context.view().batteryForward();
    std::vector<double> heading;
    heading.reserve(samples_.size());
    for (const OrientationSample &sample : samples_) {
        double value = displayHeadingFromYaw(compasscore::displayAngles(sample.q).yaw, declination);
        if (!batteryForward) {
            value = displayHeadingFromYaw(value, 180.0);
        }
        heading.push_back(value);
    }
    publishCompass(context, timeSource_, headingStream(), heading, visible);
}

bool CompassTransform::run(TransformContext &context, std::string &error)
{
    if (!loadSamples(context, error)) {
        reset();
        return false;
    }

    std::vector<double> acceleration;
    std::vector<double> pitch;
    std::vector<double> roll;
    std::vector<double> dip;
    std::vector<double> field;
    for (const OrientationSample &sample : samples_) {
        const compasscore::DisplayAngles angles = compasscore::displayAngles(sample.q);
        acceleration.push_back(sample.mg);
        pitch.push_back(angles.pitch);
        roll.push_back(angles.roll);
        dip.push_back(sample.dip);
        field.push_back(sample.field);
    }

    publishHeading(context, true);
    publishCompass(context, timeSource_,
                   compassStream("compass_acceleration", "Acceleration", "mg", Rgb{85, 110, 210},
                                 true, {true, 800.0, 1200.0}),
                   acceleration, true);
    publishCompass(context, timeSource_,
                   compassStream("compass_pitch", "Pitch", "deg", Rgb{215, 115, 40}, false,
                                 {true, -180.0, 180.0}),
                   pitch, false);
    publishCompass(context, timeSource_,
                   compassStream("compass_roll", "Roll", "deg", Rgb{115, 85, 180}, false,
                                 {true, -180.0, 180.0}),
                   roll, false);
    publishCompass(context, timeSource_,
                   compassStream("compass_dip", "Dip", "deg", Rgb{25, 150, 155}, false,
                                 {true, -90.0, 90.0}),
                   dip, false);
    publishCompass(context, timeSource_,
                   compassStream("compass_field", "Magnetic field", "uT", Rgb{105, 130, 40}, false),
                   field, false);
    return true;
}

bool CompassTransform::update(TransformContext &context, const std::string &, std::string &)
{
    // Declination and battery direction change only the heading; the
    // magnetic-frame samples stay as they are.
    if (!context.view().hasStream("compass_heading") || samples_.empty()) {
        return true;
    }
    publishHeading(context, context.visibleOnRemake("compass_heading"));
    return true;
}

// --- TransformRegistry -----------------------------------------------------

TransformRegistry TransformRegistry::makeDefault()
{
    TransformRegistry registry;
    registry.transforms_.push_back(std::make_shared<AltitudeTransform>());
    registry.transforms_.push_back(std::make_shared<CompassTransform>());
    registry.transforms_.push_back(std::make_shared<ImuMagnitudeTransform>());
    for (const auto &transform : registry.transforms_) {
        for (const ParameterSpec &spec : transform->parameters()) {
            registry.parameters_.push_back(spec);
        }
    }
    return registry;
}

Transform *TransformRegistry::find(const std::string &id) const
{
    for (const auto &transform : transforms_) {
        if (id == transform->id()) {
            return transform.get();
        }
    }
    return nullptr;
}

const CompassTransform *TransformRegistry::compass() const
{
    return dynamic_cast<const CompassTransform *>(find("compass"));
}

bool TransformRegistry::applicable(const std::string &id, const StreamCatalog &catalog,
                                   const ViewModel &view) const
{
    const Transform *transform = find(id);
    return transform && transform->applicable(catalog, view);
}

const ParameterSpec *TransformRegistry::parameter(const std::string &id) const
{
    for (const ParameterSpec &spec : parameters_) {
        if (spec.id == id) {
            return &spec;
        }
    }
    return nullptr;
}

std::vector<ParameterSpec> TransformRegistry::parameters(const StreamCatalog &catalog,
                                                         const ViewModel &view) const
{
    std::vector<ParameterSpec> specs;
    for (const auto &transform : transforms_) {
        if (transform->applicable(catalog, view)) {
            for (const ParameterSpec &spec : transform->parameters()) {
                specs.push_back(spec);
            }
        }
    }
    return specs;
}

std::vector<std::pair<std::string, std::string>> TransformRegistry::runAll(TransformContext &context)
{
    std::vector<std::pair<std::string, std::string>> failures;
    for (const auto &transform : transforms_) {
        transform->reset();
    }
    for (const auto &transform : transforms_) {
        if (!transform->applicable(context.catalog(), context.view())) {
            continue;
        }
        std::string error;
        if (!transform->run(context, error)) {
            failures.emplace_back(transform->id(), error);
        }
    }
    return failures;
}

bool TransformRegistry::setParameter(TransformContext &context, const std::string &id, double value,
                                     std::string &error)
{
    context.view().setParameter(id, value);
    for (const auto &transform : transforms_) {
        for (const ParameterSpec &spec : transform->parameters()) {
            if (spec.id != id) {
                continue;
            }
            if (!transform->applicable(context.catalog(), context.view())) {
                return true;
            }
            return transform->update(context, id, error);
        }
    }
    return true;
}

} // namespace sensorviz
