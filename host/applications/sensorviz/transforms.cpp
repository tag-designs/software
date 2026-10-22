#include "mainwindow.h"

#include <QAction>
#include <QInputDialog>
#include <QQuickWidget>
#include <QSignalBlocker>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

// transforms.cpp owns scalar display transforms that start from existing
// SensorStream objects. CompassTag record-set transforms live in
// compass_transforms.cpp because they create a family of streams and update the
// QML orientation panel.

namespace
{

// Several code paths need to synchronize QAction state with stream state.
// Blocking signals prevents those synchronization updates from recursively
// calling the slots that create/remove transforms.
void setActionCheckedSilently(QAction *action, bool checked)
{
    if (!action) {
        return;
    }

    QSignalBlocker blocker(action);
    action->setChecked(checked);
}

void updateDeclinationActionText(QAction *action, double declination_degrees)
{
    // The action is created in mainwindow.cpp but updated here because
    // transform availability and display settings are refreshed together.
    if (!action) {
        return;
    }
    action->setText(QObject::tr("&Declination... (%1 deg)").arg(declination_degrees, 0, 'f', 2));
}

void updateSeaLevelPressureActionText(QAction *action, double sea_level_pressure)
{
    // Keep the active altitude parameter visible in menus and popups.
    if (!action) {
        return;
    }
    action->setText(QObject::tr("Sea-level &Pressure... (%1 mbar)")
                        .arg(sea_level_pressure, 0, 'f', 2));
}

// First-order low-pass filter for activity display. The input time vector is in
// epoch seconds, so the filter can handle uneven sample spacing.
std::vector<double> lowPass(
    const std::vector<double> &time,
    const std::vector<double> &value,
    double time_constant_seconds)
{
    std::vector<double> filtered;
    if (time.empty() || value.empty()) {
        return filtered;
    }

    filtered.reserve(value.size());
    double previous = value.front();
    filtered.push_back(previous);

    for (size_t i = 1; i < value.size(); i++) {
        const double dt = std::max(0.0, time[i] - time[i - 1]);
        const double alpha = time_constant_seconds > 0.0
            ? dt / (time_constant_seconds + dt)
            : 1.0;
        previous = previous + alpha * (value[i] - previous);
        filtered.push_back(previous);
    }

    return filtered;
}

bool nearestValue(
    const std::vector<double> &time,
    const std::vector<double> &values,
    double time_seconds,
    double *value)
{
    // Return the nearest sample at the requested time. Pressure and
    // pressure-sensor temperature are normally sampled together, but this
    // keeps altitude robust if a future log stores them at slightly different
    // times. The caller is responsible for matching epoch/elapsed domains.
    if (!value || time.empty() || values.size() != time.size()) {
        return false;
    }

    auto it = std::lower_bound(time.cbegin(), time.cend(), time_seconds);
    size_t index = 0;
    if (it == time.cend()) {
        index = time.size() - 1;
    } else if (it == time.cbegin()) {
        index = 0;
    } else {
        const size_t after = static_cast<size_t>(std::distance(time.cbegin(), it));
        const size_t before = after - 1;
        index = std::abs(time[after] - time_seconds) < std::abs(time_seconds - time[before])
            ? after
            : before;
    }

    *value = values[index];
    return true;
}

} // namespace

// Transforms are display-only. They do not change log_ and they are not written
// back to disk; they just add/remove derived SensorStream objects. New
// transforms should follow the same pattern:
// - guard against a missing input stream
// - ask for any parameters
// - build one or more SensorStream objects
// - addOrReplaceStream() so the rest of the UI treats it like any other stream
//
// Single-output transforms such as activity low-pass use derived=true and are
// controlled by their Configuration action. Altitude and Compass output are
// generated as normal View-selectable streams; their parameters live under
// Configuration.

void MainWindow::updateTransformActions()
{
    // Centralize all data-dependent action visibility. loadLog(),
    // addOrReplaceStream(), and removeStream() call this so menus never expose
    // transforms whose inputs are missing.
    if (log_.path.isEmpty()) {
        sea_level_pressure_action_->setVisible(false);
        sea_level_pressure_action_->setEnabled(false);
        updateSeaLevelPressureActionText(sea_level_pressure_action_, view_.seaLevelPressure());

        activity_filter_action_->setVisible(false);
        activity_filter_action_->setEnabled(false);

        declination_action_->setVisible(false);
        declination_action_->setEnabled(false);
        battery_forward_action_->setVisible(false);
        battery_forward_action_->setEnabled(false);
        updateDeclinationActionText(declination_action_, view_.declinationDegrees());

        configuration_transform_separator_->setVisible(false);
        setActionCheckedSilently(activity_filter_action_, false);
        return;
    }

    const bool has_pressure = hasStream("pressure") || hasStream("imu_pressure");
    const bool has_activity = hasStream("activity");
    const bool has_compass = hasCompassOrientationSource();

    // Menus are driven by available streams instead of tag type names so logs
    // from future tags automatically expose only the relevant transforms.
    sea_level_pressure_action_->setVisible(has_pressure);
    sea_level_pressure_action_->setEnabled(has_pressure);
    updateSeaLevelPressureActionText(sea_level_pressure_action_, view_.seaLevelPressure());

    activity_filter_action_->setVisible(has_activity);
    activity_filter_action_->setEnabled(has_activity);

    declination_action_->setVisible(has_compass);
    declination_action_->setEnabled(has_compass);
    battery_forward_action_->setVisible(has_compass);
    battery_forward_action_->setEnabled(has_compass);
    updateDeclinationActionText(declination_action_, view_.declinationDegrees());
    if (compass_widget_) {
        compass_widget_->setVisible(has_compass);
    }

    configuration_transform_separator_->setVisible(has_pressure || has_activity || has_compass);

    setActionCheckedSilently(activity_filter_action_, hasStream("activity_lowpass"));
}

void MainWindow::createAltitudeStream()
{
    // Generate the pressure-derived altitude stream. It is intentionally a
    // normal View-selectable stream; sea-level pressure is the Configuration
    // value that controls its calculation.
    const SensorStream *pressure = streamById("pressure");
    if (!pressure) {
        pressure = streamById("imu_pressure");
    }
    if (!pressure) {
        return;
    }

    const bool checked = isStreamVisible("altitude");

    SensorStream altitude;
    altitude.id = "altitude";
    altitude.label = tr("Altitude").toStdString();
    altitude.units = "m";
    altitude.timeDomain = pressure->timeDomain;
    altitude.color = sensorviz::Rgb{110, 70, 180};
    altitude.defaultVisible = true;
    altitude.axisSide = SensorAxisSide::Left;

    // Altitude uses the SQLite doubles, read back through the plot backend,
    // so it is exactly what it was computed from before. Pressure is logged
    // at a low rate, so the extra pass costs little.
    std::vector<double> pressure_time;
    std::vector<double> pressure_mbar;
    if (!readStreamDoubles(*pressure, pressure_time, pressure_mbar)) {
        return;
    }
    std::vector<double> temperature_time;
    std::vector<double> temperature_c;
    const SensorStream *sensor_temperature = streamById("sensor_temperature");
    const bool have_temperature = sensor_temperature
        && sensor_temperature->timeDomain == pressure->timeDomain
        && readStreamDoubles(*sensor_temperature, temperature_time, temperature_c);

    std::vector<double> values;
    values.reserve(pressure_mbar.size());
    for (size_t i = 0; i < pressure_mbar.size(); i++) {
        double temperature = 0.0;
        if (have_temperature
            && nearestValue(temperature_time, temperature_c, pressure_time[i], &temperature)) {
            values.push_back(pressureToAltitude(pressure_mbar[i], temperature));
        } else {
            values.push_back(pressureToAltitude(pressure_mbar[i]));
        }
    }
    const std::optional<sensordata::StreamId> id =
        log_.data->addDerived(derivedPlotInfo(altitude), pressure->dataId, values);
    if (!id) {
        return;
    }
    altitude.dataId = *id;

    if (view_.hasCustomRange(pressure->id) && !view_.isExplicitRange("altitude")) {
        const sensorviz::PlotRange pressure_range = view_.customRanges().at(pressure->id);
        view_.setCustomRange(
            "altitude",
            sensorviz::PlotRange{
                pressureToAltitude(pressure_range.upper),
                pressureToAltitude(pressure_range.lower)},
            false);
    }

    addOrReplaceStream(altitude, checked);
}

void MainWindow::createImuMagnitudeStreams()
{
    // Vector magnitudes are display transforms, so they are computed from the
    // plot backend's float columns (the calibrated axes, where calibration
    // applies) without reading the doubles back. The three axes must be
    // samples of one table; otherwise no magnitude is made.
    struct Magnitude
    {
        const char *id;
        const char *label;
        const char *units;
        SensorAxisRange range;
        const char *axes[3];
    };
    const Magnitude magnitudes[] = {
        {"imu_accel_magnitude", QT_TR_NOOP("Acceleration magnitude"), QT_TR_NOOP("mg"),
         {true, 0.0, 2000.0}, {"imu_ax", "imu_ay", "imu_az"}},
        {"imu_gyro_magnitude", QT_TR_NOOP("Gyro magnitude"), QT_TR_NOOP("dps"),
         {}, {"imu_gx", "imu_gy", "imu_gz"}},
        {"imu_mag_magnitude", QT_TR_NOOP("Magnetic field magnitude"), QT_TR_NOOP("uT"),
         {}, {"imu_mx", "imu_my", "imu_mz"}},
    };

    for (const Magnitude &magnitude : magnitudes) {
        const SensorStream *x = streamById(magnitude.axes[0]);
        const SensorStream *y = streamById(magnitude.axes[1]);
        const SensorStream *z = streamById(magnitude.axes[2]);
        if (!x || !y || !z || !log_.data) {
            continue;
        }

        SensorStream stream;
        stream.id = magnitude.id;
        stream.label = tr(magnitude.label).toStdString();
        stream.units = tr(magnitude.units).toStdString();
        stream.timeDomain = x->timeDomain;
        // A magnitude shares its components' axis, so it joins their group.
        stream.group = x->group;
        stream.groupLabel = x->groupLabel;
        stream.color = sensorviz::defaultDisplayForStream(magnitude.id).color;
        stream.defaultVisible = true;
        stream.axisSide = SensorAxisSide::Left;
        stream.axisRange = magnitude.range;

        const std::optional<sensordata::StreamId> id = log_.data->addComputed(
            derivedPlotInfo(stream),
            {x->dataId, y->dataId, z->dataId},
            [](const float *v) {
                const double vx = v[0];
                const double vy = v[1];
                const double vz = v[2];
                return std::sqrt(vx * vx + vy * vy + vz * vz);
            });
        if (!id) {
            continue;
        }
        stream.dataId = *id;

        addOrReplaceStream(stream, isStreamVisible(toQString(stream.id)));
    }
}

void MainWindow::setSeaLevelPressure()
{
    // Update the altitude parameter. The altitude stream is regenerated, but
    // its current Visible Streams checked state is preserved.
    bool ok = false;
    const double sea_level = QInputDialog::getDouble(
        this,
        tr("Sea-level Pressure"),
        tr("Mean sea-level pressure (mbar)"),
        view_.seaLevelPressure(),
        800.0,
        1200.0,
        2,
        &ok);
    if (!ok) {
        return;
    }

    view_.setSeaLevelPressure(sea_level);
    createAltitudeStream();
}

void MainWindow::activityFilterToggled(bool checked)
{
    // Add/remove a first-order low-pass view of Activity. This is display-only:
    // it does not alter the raw Activity stream loaded from the database.
    if (!checked) {
        removeStream("activity_lowpass");
        return;
    }

    const SensorStream *activity = streamById("activity");
    if (!activity) {
        setActionCheckedSilently(activity_filter_action_, false);
        return;
    }

    bool ok = false;
    const double seconds = QInputDialog::getDouble(
        this,
        tr("Activity Filter"),
        tr("Time constant (seconds)"),
        view_.activityTimeConstant(),
        1.0,
        86400.0,
        1,
        &ok);
    if (!ok) {
        setActionCheckedSilently(activity_filter_action_, hasStream("activity_lowpass"));
        return;
    }
    view_.setActivityTimeConstant(seconds);

    // The filter is intentionally simple and display-only. Raw activity remains
    // available as its own stream so users can compare filtered/unfiltered data.
    // It runs on the SQLite doubles, as it always has; activity logs are small.
    SensorStream filtered;
    filtered.id = "activity_lowpass";
    filtered.label = tr("Activity filter").toStdString();
    filtered.units = activity->units;
    filtered.timeDomain = activity->timeDomain;
    filtered.color = sensorviz::Rgb{20, 60, 120};
    filtered.derived = true;
    filtered.axisSide = activity->axisSide;
    filtered.axisRange = activity->axisRange;

    std::vector<double> activity_time;
    std::vector<double> activity_value;
    std::optional<sensordata::StreamId> id;
    if (readStreamDoubles(*activity, activity_time, activity_value)) {
        id = log_.data->addDerived(
            derivedPlotInfo(filtered),
            activity->dataId,
            lowPass(activity_time, activity_value, view_.activityTimeConstant()));
    }
    if (!id) {
        setActionCheckedSilently(activity_filter_action_, hasStream("activity_lowpass"));
        return;
    }
    filtered.dataId = *id;

    if (view_.hasCustomRange("activity") && !view_.isExplicitRange("activity_lowpass")) {
        view_.setCustomRange("activity_lowpass", view_.customRanges().at("activity"), false);
    }

    addOrReplaceStream(filtered, true);
}
