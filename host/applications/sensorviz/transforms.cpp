#include "mainwindow.h"

#include "compass_processor.h"

#include <QAction>
#include <QInputDialog>
#include <QMessageBox>
#include <QSignalBlocker>

#include <algorithm>

namespace
{

const QStringList compassDerivedStreamIds()
{
    return {
        "compass_heading",
        "compass_acceleration",
        "compass_pitch",
        "compass_roll",
        "compass_dip",
        "compass_field",
    };
}

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
    if (!action) {
        return;
    }
    action->setText(QObject::tr("&Declination... (%1 deg)").arg(declination_degrees, 0, 'f', 2));
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

SensorStream makeCompassStream(
    const QString &id,
    const QString &label,
    const QString &units,
    const QColor &color,
    const QVector<double> &time,
    const QVector<double> &value,
    bool default_visible,
    const SensorAxisRange &range = {})
{
    SensorStream stream;
    stream.id = id;
    stream.label = label;
    stream.units = units;
    stream.time = time;
    stream.value = value;
    stream.color = color;
    stream.defaultVisible = default_visible;
    stream.axisRange = range;
    return stream;
}

bool compassRecordHasExpectedColumns(const SensorRecordSet &compass)
{
    const qsizetype sample_count = compass.time.size();
    return compass.columns.value("ax").size() == sample_count
        && compass.columns.value("ay").size() == sample_count
        && compass.columns.value("az").size() == sample_count
        && compass.columns.value("mx").size() == sample_count
        && compass.columns.value("my").size() == sample_count
        && compass.columns.value("mz").size() == sample_count;
}

QVector<double> compassHeadingValues(
    const SensorRecordSet &compass,
    const CompassCalibration &calibration,
    double declination_degrees)
{
    const QVector<double> ax = compass.columns.value("ax");
    const QVector<double> ay = compass.columns.value("ay");
    const QVector<double> az = compass.columns.value("az");
    const QVector<double> mx = compass.columns.value("mx");
    const QVector<double> my = compass.columns.value("my");
    const QVector<double> mz = compass.columns.value("mz");

    QVector<double> heading;
    heading.reserve(compass.time.size());

    CompassProcessor processor(calibration);
    for (qsizetype i = 0; i < compass.time.size(); i++) {
        CompassRawSample raw;
        raw.epoch = compass.time[i];
        raw.accel = QVector3D(ax[i], ay[i], az[i]);
        raw.mag = QVector3D(mx[i], my[i], mz[i]);

        const CompassDerivedSample sample = processor.deriveSample(raw);
        heading.append(CompassProcessor::headingFromYaw(sample.yaw, declination_degrees));
    }

    return heading;
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
// Single-output transforms such as altitude use derived=true and are controlled
// entirely by their Configuration action. Compass output is a generated family
// of normal View-selectable streams, so users can show heading, acceleration,
// pitch, roll, dip, and field independently after the family is enabled.

void MainWindow::updateTransformActions()
{
    const bool has_pressure = hasStream("pressure");
    const bool has_activity = hasStream("activity");
    const bool has_compass =
        log_.hasCompassCalibration && recordSetById("compass_raw") != nullptr;

    // Menus are driven by available streams instead of tag type names so logs
    // from future tags automatically expose only the relevant transforms.
    altitude_action_->setVisible(has_pressure);
    altitude_action_->setEnabled(has_pressure);

    activity_filter_action_->setVisible(has_activity);
    activity_filter_action_->setEnabled(has_activity);

    compass_derived_action_->setVisible(has_compass);
    compass_derived_action_->setEnabled(has_compass);
    declination_action_->setVisible(has_compass);
    declination_action_->setEnabled(has_compass);
    updateDeclinationActionText(declination_action_, declination_degrees_);

    configuration_transform_separator_->setVisible(has_pressure || has_activity || has_compass);

    setActionCheckedSilently(altitude_action_, hasStream("altitude"));
    setActionCheckedSilently(activity_filter_action_, hasStream("activity_lowpass"));
    setActionCheckedSilently(compass_derived_action_, hasStream("compass_heading"));
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

    if (custom_axis_ranges_.contains("activity")
        && !explicit_axis_ranges_.contains("activity_lowpass")) {
        custom_axis_ranges_["activity_lowpass"] = custom_axis_ranges_["activity"];
    }

    addOrReplaceStream(filtered, true);
}

void MainWindow::compassDerivedToggled(bool checked)
{
    if (!checked) {
        for (const QString &id : compassDerivedStreamIds()) {
            removeStream(id);
        }
        return;
    }

    const SensorRecordSet *compass = recordSetById("compass_raw");
    if (!compass || !log_.hasCompassCalibration) {
        QMessageBox::warning(
            this,
            tr("Compass Derived Streams"),
            tr("Compass raw samples and calibration constants are required."));
        setActionCheckedSilently(compass_derived_action_, false);
        return;
    }

    const qsizetype sample_count = compass->time.size();
    if (!compassRecordHasExpectedColumns(*compass)) {
        QMessageBox::warning(
            this,
            tr("Compass Derived Streams"),
            tr("The compass raw record set is missing one or more expected columns."));
        setActionCheckedSilently(compass_derived_action_, false);
        return;
    }

    QVector<double> heading;
    QVector<double> acceleration;
    QVector<double> pitch;
    QVector<double> roll;
    QVector<double> dip;
    QVector<double> field;
    heading = compassHeadingValues(*compass, log_.compassCalibration, declination_degrees_);
    acceleration.reserve(sample_count);
    pitch.reserve(sample_count);
    roll.reserve(sample_count);
    dip.reserve(sample_count);
    field.reserve(sample_count);

    const QVector<double> ax = compass->columns.value("ax");
    const QVector<double> ay = compass->columns.value("ay");
    const QVector<double> az = compass->columns.value("az");
    const QVector<double> mx = compass->columns.value("mx");
    const QVector<double> my = compass->columns.value("my");
    const QVector<double> mz = compass->columns.value("mz");

    CompassProcessor processor(log_.compassCalibration);
    for (qsizetype i = 0; i < sample_count; i++) {
        CompassRawSample raw;
        raw.epoch = compass->time[i];
        raw.accel = QVector3D(ax[i], ay[i], az[i]);
        raw.mag = QVector3D(mx[i], my[i], mz[i]);

        const CompassDerivedSample sample = processor.deriveSample(raw);
        acceleration.append(sample.mg);
        pitch.append(sample.pitch);
        roll.append(sample.roll);
        dip.append(sample.dip);
        field.append(sample.field);
    }

    addOrReplaceStream(
        makeCompassStream(
            "compass_heading",
            tr("Heading"),
            tr("deg"),
            QColor(195, 40, 175),
            compass->time,
            heading,
            true,
            {true, 0.0, 360.0}),
        true);
    addOrReplaceStream(
        makeCompassStream(
            "compass_acceleration",
            tr("Acceleration"),
            tr("mg"),
            QColor(85, 110, 210),
            compass->time,
            acceleration,
            true),
        true);
    addOrReplaceStream(
        makeCompassStream(
            "compass_pitch",
            tr("Pitch"),
            tr("deg"),
            QColor(215, 115, 40),
            compass->time,
            pitch,
            false,
            {true, -180.0, 180.0}),
        false);
    addOrReplaceStream(
        makeCompassStream(
            "compass_roll",
            tr("Roll"),
            tr("deg"),
            QColor(115, 85, 180),
            compass->time,
            roll,
            false,
            {true, -180.0, 180.0}),
        false);
    addOrReplaceStream(
        makeCompassStream(
            "compass_dip",
            tr("Dip"),
            tr("deg"),
            QColor(25, 150, 155),
            compass->time,
            dip,
            false,
            {true, -90.0, 90.0}),
        false);
    addOrReplaceStream(
        makeCompassStream(
            "compass_field",
            tr("Magnetic field"),
            QString(),
            QColor(105, 130, 40),
            compass->time,
            field,
            false),
        false);
}

void MainWindow::setDeclination()
{
    bool ok = false;
    const double declination = QInputDialog::getDouble(
        this,
        tr("Declination"),
        tr("Declination angle (degrees)"),
        declination_degrees_,
        -180.0,
        180.0,
        2,
        &ok);
    if (!ok) {
        return;
    }

    declination_degrees_ = declination;
    updateDeclinationActionText(declination_action_, declination_degrees_);

    const SensorRecordSet *compass = recordSetById("compass_raw");
    if (!compass || !log_.hasCompassCalibration || !hasStream("compass_heading")) {
        return;
    }
    if (!compassRecordHasExpectedColumns(*compass)) {
        return;
    }

    const QAction *heading_action = streamActionById("compass_heading");
    const bool checked = heading_action ? heading_action->isChecked() : true;
    addOrReplaceStream(
        makeCompassStream(
            "compass_heading",
            tr("Heading"),
            tr("deg"),
            QColor(195, 40, 175),
            compass->time,
            compassHeadingValues(*compass, log_.compassCalibration, declination_degrees_),
            true,
            {true, 0.0, 360.0}),
        checked);
}
