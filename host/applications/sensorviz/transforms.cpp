#include "mainwindow.h"

#include <QAction>
#include <QInputDialog>
#include <QQuickWidget>
#include <QSignalBlocker>

#include <algorithm>

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

bool nearestStreamValue(const SensorStream &stream, double epoch, double *value)
{
    // Return the nearest sample in a stream at the requested epoch. Pressure
    // and pressure-sensor temperature are normally sampled together, but this
    // keeps altitude robust if a future log stores them at slightly different
    // times.
    if (!value || stream.time.isEmpty() || stream.value.isEmpty()) {
        return false;
    }

    auto it = std::lower_bound(stream.time.cbegin(), stream.time.cend(), epoch);
    qsizetype index = 0;
    if (it == stream.time.cend()) {
        index = stream.time.size() - 1;
    } else if (it == stream.time.cbegin()) {
        index = 0;
    } else {
        const qsizetype after = std::distance(stream.time.cbegin(), it);
        const qsizetype before = after - 1;
        index = std::abs(stream.time[after] - epoch) < std::abs(epoch - stream.time[before])
            ? after
            : before;
    }
    if (index < 0 || index >= stream.value.size()) {
        return false;
    }

    *value = stream.value[index];
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
    const bool has_pressure = hasStream("pressure");
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
        return;
    }

    const QAction *altitude_action = streamActionById("altitude");
    const bool checked = altitude_action ? altitude_action->isChecked() : false;

    SensorStream altitude;
    altitude.id = "altitude";
    altitude.label = tr("Altitude");
    altitude.units = "m";
    altitude.time = pressure->time;
    altitude.color = QColor(110, 70, 180);
    altitude.defaultVisible = false;
    altitude.value.reserve(pressure->value.size());
    const SensorStream *sensor_temperature = streamById("sensor_temperature");
    for (qsizetype i = 0; i < pressure->value.size(); i++) {
        const double pressure_mbar = pressure->value[i];
        double temperature_c = 0.0;
        if (sensor_temperature
            && i < pressure->time.size()
            && nearestStreamValue(*sensor_temperature, pressure->time[i], &temperature_c)) {
            altitude.value.append(pressureToAltitude(pressure_mbar, temperature_c));
        } else {
            altitude.value.append(pressureToAltitude(pressure_mbar));
        }
    }

    if (custom_axis_ranges_.contains("pressure") && !explicit_axis_ranges_.contains("altitude")) {
        const QCPRange pressure_range = custom_axis_ranges_["pressure"];
        custom_axis_ranges_["altitude"] =
            QCPRange(
                pressureToAltitude(pressure_range.upper),
                pressureToAltitude(pressure_range.lower));
    }

    addOrReplaceStream(altitude, checked);
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
