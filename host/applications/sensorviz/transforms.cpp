#include "mainwindow.h"

#include <QAction>
#include <QInputDialog>
#include <QSignalBlocker>

#include <algorithm>

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

} // namespace

// Transforms are display-only. They do not change log_ and they are not written
// back to disk; they just add/remove derived SensorStream objects. New
// transforms should follow the same pattern:
// - guard against a missing input stream
// - ask for any parameters
// - build a SensorStream with derived=true
// - addOrReplaceStream() so the rest of the UI treats it like any other stream

void MainWindow::updateTransformActions()
{
    const bool has_pressure = hasStream("pressure");
    const bool has_activity = hasStream("activity");

    // Menus are driven by available streams instead of tag type names so logs
    // from future tags automatically expose only the relevant transforms.
    altitude_action_->setVisible(has_pressure);
    altitude_action_->setEnabled(has_pressure);

    activity_filter_action_->setVisible(has_activity);
    activity_filter_action_->setEnabled(has_activity);

    configuration_transform_separator_->setVisible(has_pressure || has_activity);

    setActionCheckedSilently(altitude_action_, hasStream("altitude"));
    setActionCheckedSilently(activity_filter_action_, hasStream("activity_lowpass"));
}

void MainWindow::altitudeToggled(bool checked)
{
    if (!checked) {
        removeStream("altitude");
        return;
    }

    const SensorStream *pressure = streamById("pressure");
    if (!pressure) {
        setActionCheckedSilently(altitude_action_, false);
        return;
    }

    bool ok = false;
    const double sea_level = QInputDialog::getDouble(
        this,
        tr("Altitude"),
        tr("Sea-level pressure (mbar)"),
        sea_level_pressure_,
        800.0,
        1200.0,
        2,
        &ok);
    if (!ok) {
        setActionCheckedSilently(altitude_action_, hasStream("altitude"));
        return;
    }
    sea_level_pressure_ = sea_level;

    // Altitude is a derived pressure stream. It stays linked by convention to
    // the pressure axis range unless the user sets an explicit altitude range.
    SensorStream altitude;
    altitude.id = "altitude";
    altitude.label = tr("Altitude");
    altitude.units = "m";
    altitude.time = pressure->time;
    altitude.color = QColor(110, 70, 180);
    altitude.derived = true;
    altitude.value.reserve(pressure->value.size());
    for (double pressure_mbar : pressure->value) {
        altitude.value.append(pressureToAltitude(pressure_mbar));
    }

    if (custom_axis_ranges_.contains("pressure") && !explicit_axis_ranges_.contains("altitude")) {
        const QCPRange pressure_range = custom_axis_ranges_["pressure"];
        custom_axis_ranges_["altitude"] =
            QCPRange(
                pressureToAltitude(pressure_range.upper),
                pressureToAltitude(pressure_range.lower));
    }

    addOrReplaceStream(altitude, true);
}

void MainWindow::activityFilterToggled(bool checked)
{
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

    addOrReplaceStream(filtered, true);
}
