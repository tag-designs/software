#include "mainwindow.h"

#include "compass_processor.h"

#include <QAction>
#include <QInputDialog>
#include <QMessageBox>
#include <QSignalBlocker>

// compass_transforms.cpp owns CompassTag-specific display derivation. It is the
// bridge between SensorRecordSet compass_raw, sensoranalysis::CompassProcessor,
// ordinary SensorStream plots, and the optional QML compass panel.

namespace
{

// The generated stream ids are kept together so disabling the Compass Derived
// Streams action can remove the whole family without duplicating strings.
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

void setActionCheckedSilently(QAction *action, bool checked)
{
    // Synchronize check state from model state without recursively invoking the
    // toggled slot that creates/removes streams.
    if (!action) {
        return;
    }

    QSignalBlocker blocker(action);
    action->setChecked(checked);
}

void updateDeclinationActionText(QAction *action, double declination_degrees)
{
    // Keep the current numeric declination visible in both the top-level menu
    // and any popup menu that reuses this QAction.
    if (!action) {
        return;
    }
    action->setText(QObject::tr("&Declination... (%1 deg)").arg(declination_degrees, 0, 'f', 2));
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
    // Construct the normal SensorStream objects that plotting, range controls,
    // tooltips, and View > Visible Streams already understand.
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
    // Validate the loaded record set before indexing parallel columns. Missing
    // columns usually mean a schema drift that needs loader/profile work.
    const qsizetype sample_count = compass.time.size();
    return compass.columns.value("ax").size() == sample_count
        && compass.columns.value("ay").size() == sample_count
        && compass.columns.value("az").size() == sample_count
        && compass.columns.value("mx").size() == sample_count
        && compass.columns.value("my").size() == sample_count
        && compass.columns.value("mz").size() == sample_count;
}

QVector<double> compassHeadingValues(
    const QVector<CompassDerivedSample> &samples,
    double declination_degrees,
    bool battery_forward)
{
    // Heading is the only CompassTag stream affected by display settings.
    // Samples remain magnetic-frame; declination and battery direction are
    // layered on here and in the QML panel.
    QVector<double> heading;
    heading.reserve(samples.size());
    for (const CompassDerivedSample &sample : samples) {
        double value = CompassProcessor::headingFromYaw(sample.yaw, declination_degrees);
        if (!battery_forward) {
            value = CompassProcessor::headingFromYaw(value, 180.0);
        }
        heading.append(value);
    }

    return heading;
}

} // namespace

// CompassTag transforms are kept separate from the simpler scalar transforms
// because one raw record set expands into several view-selectable streams plus
// a specialized QML display. Loaded samples remain magnetic-frame; declination
// and battery direction are display choices applied to heading and to QML.

void MainWindow::compassDerivedToggled(bool checked)
{
    // Turn the raw compass record set into plot-ready streams. The first enable
    // also fills compass_samples_, which the QML panel uses on mouse movement.
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
    acceleration.reserve(sample_count);
    pitch.reserve(sample_count);
    roll.reserve(sample_count);
    dip.reserve(sample_count);
    field.reserve(sample_count);
    compass_samples_.clear();
    compass_samples_.reserve(sample_count);

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
        compass_samples_.append(sample);
        acceleration.append(sample.mg);
        pitch.append(sample.pitch);
        roll.append(sample.roll);
        dip.append(sample.dip);
        field.append(sample.field);
    }
    heading = compassHeadingValues(compass_samples_, declination_degrees_, battery_forward_);
    if (!compass_samples_.isEmpty()) {
        compass_display_.showSample(compass_samples_.first());
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
            true,
            {true, 800.0, 1200.0}),
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
            tr("uT"),
            QColor(105, 130, 40),
            compass->time,
            field,
            false),
        false);
}

void MainWindow::setDeclination()
{
    // Update the user-selected true-north offset. Only the heading stream and
    // QML display change; raw derived samples in compass_samples_ are preserved.
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
    compass_display_.setDeclination(declination_degrees_);

    const SensorRecordSet *compass = recordSetById("compass_raw");
    if (!compass || !log_.hasCompassCalibration || !hasStream("compass_heading")) {
        return;
    }
    if (!compassRecordHasExpectedColumns(*compass) || compass_samples_.isEmpty()) {
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
            compassHeadingValues(compass_samples_, declination_degrees_, battery_forward_),
            true,
            {true, 0.0, 360.0}),
        checked);
}

void MainWindow::batteryForwardToggled(bool checked)
{
    // Battery direction is a display convention inherited from compviz. It
    // rotates heading by 180 degrees and tells QML how to orient the tag model.
    battery_forward_ = checked;
    compass_display_.setBatteryForward(battery_forward_);

    const SensorRecordSet *compass = recordSetById("compass_raw");
    if (!compass || !hasStream("compass_heading") || compass_samples_.isEmpty()) {
        return;
    }

    const QAction *heading_action = streamActionById("compass_heading");
    const bool heading_checked = heading_action ? heading_action->isChecked() : true;
    addOrReplaceStream(
        makeCompassStream(
            "compass_heading",
            tr("Heading"),
            tr("deg"),
            QColor(195, 40, 175),
            compass->time,
            compassHeadingValues(compass_samples_, declination_degrees_, battery_forward_),
            true,
            {true, 0.0, 360.0}),
        heading_checked);
}
