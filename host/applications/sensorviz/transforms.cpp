#include "mainwindow.h"

#include <QAction>
#include <QInputDialog>
#include <QQuickWidget>
#include <QSignalBlocker>

#include <algorithm>
#include <cmath>

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
QVector<double> lowPass(
    const QVector<double> &time,
    const QVector<double> &value,
    double time_constant_seconds)
{
    QVector<double> filtered;
    if (time.isEmpty() || value.isEmpty()) {
        return filtered;
    }

    filtered.reserve(value.size());
    double previous = value.first();
    filtered.append(previous);

    for (qsizetype i = 1; i < value.size(); i++) {
        const double dt = std::max(0.0, time[i] - time[i - 1]);
        const double alpha = time_constant_seconds > 0.0
            ? dt / (time_constant_seconds + dt)
            : 1.0;
        previous = previous + alpha * (value[i] - previous);
        filtered.append(previous);
    }

    return filtered;
}

bool nearestStreamValue(const SensorStream &stream, double time_seconds, double *value)
{
    // Return the nearest sample in a stream at the requested time. Pressure
    // and pressure-sensor temperature are normally sampled together, but this
    // keeps altitude robust if a future log stores them at slightly different
    // times. The caller is responsible for matching epoch/elapsed domains.
    if (!value || stream.time.isEmpty() || stream.value.isEmpty()) {
        return false;
    }

    auto it = std::lower_bound(stream.time.cbegin(), stream.time.cend(), time_seconds);
    qsizetype index = 0;
    if (it == stream.time.cend()) {
        index = stream.time.size() - 1;
    } else if (it == stream.time.cbegin()) {
        index = 0;
    } else {
        const qsizetype after = std::distance(stream.time.cbegin(), it);
        const qsizetype before = after - 1;
        index = std::abs(stream.time[after] - time_seconds)
                < std::abs(time_seconds - stream.time[before])
            ? after
            : before;
    }
    if (index < 0 || index >= stream.value.size()) {
        return false;
    }

    *value = stream.value[index];
    return true;
}

bool streamsHaveSameTimebase(
    const SensorStream &x,
    const SensorStream &y,
    const SensorStream &z)
{
    if (x.timeDomain != y.timeDomain || x.timeDomain != z.timeDomain) {
        return false;
    }
    if (x.time.size() != y.time.size() || x.time.size() != z.time.size()) {
        return false;
    }
    for (qsizetype i = 0; i < x.time.size(); i++) {
        if (x.time[i] != y.time[i] || x.time[i] != z.time[i]) {
            return false;
        }
    }
    return true;
}

SensorStream makeMagnitudeStream(
    const QString &id,
    const QString &label,
    const QString &units,
    const QColor &color,
    const SensorStream &x,
    const SensorStream &y,
    const SensorStream &z,
    bool default_visible,
    const SensorAxisRange &range = {})
{
    SensorStream stream;
    stream.id = id;
    stream.label = label;
    stream.units = units;
    stream.time = x.time;
    stream.timeDomain = x.timeDomain;
    stream.color = color;
    stream.defaultVisible = default_visible;
    stream.axisSide = SensorAxisSide::Left;
    stream.axisRange = range;
    stream.value.reserve(x.value.size());

    for (qsizetype i = 0; i < x.value.size(); i++) {
        const double vx = x.value[i];
        const double vy = y.value[i];
        const double vz = z.value[i];
        stream.value.append(std::sqrt(vx * vx + vy * vy + vz * vz));
    }

    return stream;
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
        updateSeaLevelPressureActionText(sea_level_pressure_action_, sea_level_pressure_);

        activity_filter_action_->setVisible(false);
        activity_filter_action_->setEnabled(false);

        declination_action_->setVisible(false);
        declination_action_->setEnabled(false);
        battery_forward_action_->setVisible(false);
        battery_forward_action_->setEnabled(false);
        updateDeclinationActionText(declination_action_, declination_degrees_);

        configuration_transform_separator_->setVisible(false);
        setActionCheckedSilently(activity_filter_action_, false);
        return;
    }

    const bool has_pressure = hasStream("pressure") || hasStream("imu_pressure");
    const bool has_activity = hasStream("activity");
    const bool has_compass =
        log_.hasCompassCalibration && recordSetById("compass_raw") != nullptr;

    // Menus are driven by available streams instead of tag type names so logs
    // from future tags automatically expose only the relevant transforms.
    sea_level_pressure_action_->setVisible(has_pressure);
    sea_level_pressure_action_->setEnabled(has_pressure);
    updateSeaLevelPressureActionText(sea_level_pressure_action_, sea_level_pressure_);

    activity_filter_action_->setVisible(has_activity);
    activity_filter_action_->setEnabled(has_activity);

    declination_action_->setVisible(has_compass);
    declination_action_->setEnabled(has_compass);
    battery_forward_action_->setVisible(has_compass);
    battery_forward_action_->setEnabled(has_compass);
    updateDeclinationActionText(declination_action_, declination_degrees_);
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

    const QAction *altitude_action = streamActionById("altitude");
    const bool checked = altitude_action ? altitude_action->isChecked() : true;

    SensorStream altitude;
    altitude.id = "altitude";
    altitude.label = tr("Altitude");
    altitude.units = "m";
    altitude.time = pressure->time;
    altitude.timeDomain = pressure->timeDomain;
    altitude.color = QColor(110, 70, 180);
    altitude.defaultVisible = true;
    altitude.axisSide = SensorAxisSide::Left;
    altitude.value.reserve(pressure->value.size());
    const SensorStream *sensor_temperature = streamById("sensor_temperature");
    for (qsizetype i = 0; i < pressure->value.size(); i++) {
        const double pressure_mbar = pressure->value[i];
        double temperature_c = 0.0;
        if (sensor_temperature
            && sensor_temperature->timeDomain == pressure->timeDomain
            && i < pressure->time.size()
            && nearestStreamValue(*sensor_temperature, pressure->time[i], &temperature_c)) {
            altitude.value.append(pressureToAltitude(pressure_mbar, temperature_c));
        } else {
            altitude.value.append(pressureToAltitude(pressure_mbar));
        }
    }

    if (custom_axis_ranges_.contains(pressure->id)
        && !explicit_axis_ranges_.contains("altitude")) {
        const QCPRange pressure_range = custom_axis_ranges_[pressure->id];
        custom_axis_ranges_["altitude"] =
            QCPRange(
                pressureToAltitude(pressure_range.upper),
                pressureToAltitude(pressure_range.lower));
    }

    addOrReplaceStream(altitude, checked);
}

void MainWindow::createImuMagnitudeStreams()
{
    const SensorStream *ax = streamById("imu_ax");
    const SensorStream *ay = streamById("imu_ay");
    const SensorStream *az = streamById("imu_az");
    if (ax && ay && az && streamsHaveSameTimebase(*ax, *ay, *az)) {
        const QAction *action = streamActionById("imu_accel_magnitude");
        const bool checked = action ? action->isChecked() : true;
        addOrReplaceStream(
            makeMagnitudeStream(
                "imu_accel_magnitude",
                tr("Acceleration magnitude"),
                tr("mg"),
                QColor(85, 110, 210),
                *ax,
                *ay,
                *az,
                true,
                {true, 0.0, 2000.0}),
            checked);
    }

    const SensorStream *gx = streamById("imu_gx");
    const SensorStream *gy = streamById("imu_gy");
    const SensorStream *gz = streamById("imu_gz");
    if (gx && gy && gz && streamsHaveSameTimebase(*gx, *gy, *gz)) {
        const QAction *action = streamActionById("imu_gyro_magnitude");
        const bool checked = action ? action->isChecked() : true;
        addOrReplaceStream(
            makeMagnitudeStream(
                "imu_gyro_magnitude",
                tr("Gyro magnitude"),
                tr("dps"),
                QColor(190, 80, 55),
                *gx,
                *gy,
                *gz,
                true),
            checked);
    }

    const SensorStream *mx = streamById("imu_mx");
    const SensorStream *my = streamById("imu_my");
    const SensorStream *mz = streamById("imu_mz");
    if (mx && my && mz && streamsHaveSameTimebase(*mx, *my, *mz)) {
        const QAction *action = streamActionById("imu_mag_magnitude");
        const bool checked = action ? action->isChecked() : true;
        addOrReplaceStream(
            makeMagnitudeStream(
                "imu_mag_magnitude",
                tr("Magnetic field magnitude"),
                tr("uT"),
                QColor(105, 130, 40),
                *mx,
                *my,
                *mz,
                true),
            checked);
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
        sea_level_pressure_,
        800.0,
        1200.0,
        2,
        &ok);
    if (!ok) {
        return;
    }

    sea_level_pressure_ = sea_level;
    updateSeaLevelPressureActionText(sea_level_pressure_action_, sea_level_pressure_);
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
        activity_low_pass_seconds_,
        1.0,
        86400.0,
        1,
        &ok);
    if (!ok) {
        setActionCheckedSilently(activity_filter_action_, hasStream("activity_lowpass"));
        return;
    }
    activity_low_pass_seconds_ = seconds;

    // The filter is intentionally simple and display-only. Raw activity remains
    // available as its own stream so users can compare filtered/unfiltered data.
    SensorStream filtered;
    filtered.id = "activity_lowpass";
    filtered.label = tr("Activity filter");
    filtered.units = activity->units;
    filtered.time = activity->time;
    filtered.timeDomain = activity->timeDomain;
    filtered.value = lowPass(activity->time, activity->value, activity_low_pass_seconds_);
    filtered.color = QColor(20, 60, 120);
    filtered.derived = true;
    filtered.axisSide = activity->axisSide;
    filtered.axisRange = activity->axisRange;

    if (custom_axis_ranges_.contains("activity")
        && !explicit_axis_ranges_.contains("activity_lowpass")) {
        custom_axis_ranges_["activity_lowpass"] = custom_axis_ranges_["activity"];
    }

    addOrReplaceStream(filtered, true);
}
