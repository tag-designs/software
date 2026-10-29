#include "mainwindow.h"

#include <QAction>
#include <QInputDialog>
#include <QMessageBox>
#include <QQuickWidget>
#include <QSignalBlocker>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

// transforms.cpp is the Qt side of the display transforms. Altitude, the IMU
// magnitudes and compass orientation are the core's (core/transforms.h): this
// file runs the registry after a load, asks for parameters with one dialog
// built from each parameter's spec, and keeps the Configuration actions in
// step. The activity filter is still made here, in Qt; it is not ported to
// the core and leaves with this app. The compass panel is in
// compass_transforms.cpp.

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
    if (log_.path.empty()) {
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
    const bool has_compass = session_.transforms().applicable("compass", log_, view_);

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

// The sink the core's transforms publish their outputs into: each output is
// added as File > Load and the Configuration actions always added it, one
// redraw per output.
class MainWindow::TransformSink : public sensorviz::TransformSink
{
public:
    explicit TransformSink(MainWindow &window) : window_(window) {}
    void publish(const SensorStream &stream, bool visible) override
    {
        window_.addOrReplaceStream(stream, visible);
    }
    void warn(const std::string &message) override
    {
        qWarning().noquote() << toQString(message);
    }

private:
    MainWindow &window_;
};

void MainWindow::runTransforms()
{
    // Altitude, compass orientation and the IMU magnitudes, in that order,
    // for the log just loaded.
    TransformSink sink(*this);
    const auto failures = session_.runTransforms(sink);
    loadCompassPanelSamples();
    for (const auto &[id, error] : failures) {
        if (id == "compass") {
            QMessageBox::warning(this, tr("Compass Streams"), toQString(error));
        } else {
            qWarning().noquote() << toQString(id) << ":" << toQString(error);
        }
    }
}

void MainWindow::setTransformParameter(const char *id, double value)
{
    // The ViewModel takes the value and the owning transform remakes what it
    // affects.
    TransformSink sink(*this);
    std::string error;
    if (!session_.setParameter(sink, id, value, error)) {
        qWarning().noquote() << toQString(id) << ":" << toQString(error);
    }
}

bool MainWindow::askParameter(const char *id, double &value)
{
    // The one dialog for every numeric transform parameter, built from the
    // parameter's spec.
    const sensorviz::ParameterSpec *spec = session_.transforms().parameter(id);
    if (!spec || spec->kind != sensorviz::ParameterSpec::Kind::Number) {
        return false;
    }
    bool ok = false;
    value = QInputDialog::getDouble(
        this,
        tr(spec->title.c_str()),
        tr(spec->prompt.c_str()),
        view_.parameter(id),
        spec->minimum,
        spec->maximum,
        spec->decimals,
        &ok);
    return ok;
}

void MainWindow::setSeaLevelPressure()
{
    // Update the altitude parameter. The altitude stream is regenerated, but
    // its current Visible Streams checked state is preserved.
    double sea_level = 0.0;
    if (askParameter(sensorviz::ViewModel::kSeaLevelPressure, sea_level)) {
        applySeaLevelPressure(sea_level);
    }
}

void MainWindow::applySeaLevelPressure(double sea_level_mbar)
{
    setTransformParameter(sensorviz::ViewModel::kSeaLevelPressure, sea_level_mbar);
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
    enableActivityFilter(seconds);
}

void MainWindow::enableActivityFilter(double time_constant_seconds)
{
    const SensorStream *activity = streamById("activity");
    if (!activity) {
        setActionCheckedSilently(activity_filter_action_, false);
        return;
    }
    view_.setActivityTimeConstant(time_constant_seconds);

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
            sensorviz::derivedStreamInfo(filtered),
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
