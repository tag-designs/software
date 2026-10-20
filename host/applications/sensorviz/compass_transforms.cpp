#include "mainwindow.h"

#include "compass_angles.h"
#include "compass_processor.h"

#include <QAction>
#include <QQuaternion>
#include <QInputDialog>
#include <QMessageBox>

#include <cmath>

// compass_transforms.cpp owns compass display derivation. It is the bridge
// between the orientation sources -- a processed log's stored quaternions, or
// SensorRecordSet compass_raw through sensoranalysis::CompassProcessor -- and
// ordinary SensorStream plots and the optional QML compass panel.

namespace
{

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
    // columns usually mean a schema drift in tagcore's streams metadata or the
    // loader's record-set handling.
    const qsizetype sample_count = compass.time.size();
    return compass.columns.value("ax").size() == sample_count
        && compass.columns.value("ay").size() == sample_count
        && compass.columns.value("az").size() == sample_count
        && compass.columns.value("mx").size() == sample_count
        && compass.columns.value("my").size() == sample_count
        && compass.columns.value("mz").size() == sample_count;
}

// Processed logs (dataprocessing processor version 2) carry orientation as a
// record set of quaternion, dip, field and acceleration columns. The CompassTag
// set comes first: a processed CompassTag log also has compass_raw, and the
// stored quaternions are what the file says.
const char *const kStoredOrientationRecordSets[] = {
    "compass_orientation",
    "imu_compass_orientation",
};

bool orientationRecordHasExpectedColumns(const SensorRecordSet &orientation)
{
    const qsizetype sample_count = orientation.time.size();
    for (const char *column : {"qw", "qx", "qy", "qz", "dip", "field", "acceleration"}) {
        if (orientation.columns.value(column).size() != sample_count) {
            return false;
        }
    }
    return true;
}

QVector<double> compassSampleTimes(const QVector<CompassDerivedSample> &samples)
{
    QVector<double> time;
    time.reserve(samples.size());
    for (const CompassDerivedSample &sample : samples) {
        time.append(sample.epoch);
    }
    return time;
}

double displayHeadingFromYaw(double yaw, double declination_degrees)
{
    return std::fmod(720.0 + yaw + declination_degrees, 360.0);
}

QVector<double> compassHeadingValues(
    const QVector<CompassDerivedSample> &samples,
    double declination_degrees,
    bool battery_forward)
{
    // Heading is display policy, not calibration math. SensorViz layers
    // declination and battery direction onto the magnetic-frame yaw derived
    // from the sensoranalysis quaternion; DataProcessing stores only the
    // quaternion and documents this conversion for downstream tools.
    QVector<double> heading;
    heading.reserve(samples.size());
    for (const CompassDerivedSample &sample : samples) {
        const double yaw = compassAnglesFromQuaternion(sample.q).yaw;
        double value = displayHeadingFromYaw(yaw, declination_degrees);
        if (!battery_forward) {
            value = displayHeadingFromYaw(value, 180.0);
        }
        heading.append(value);
    }

    return heading;
}

} // namespace

// Compass transforms are kept separate from the simpler scalar transforms
// because one record set expands into several view-selectable streams plus a
// specialized QML display. Every compass view is built from orientation
// quaternions, whether they were stored by dataprocessing or derived here from
// raw CompassTag rows; heading, pitch and roll are computed from the
// quaternion at the point of display. Loaded samples remain magnetic-frame;
// declination and battery direction are display choices applied to heading
// and to QML.

const SensorRecordSet *MainWindow::storedOrientationRecordSet() const
{
    for (const char *id : kStoredOrientationRecordSets) {
        if (const SensorRecordSet *record_set = recordSetById(id)) {
            return record_set;
        }
    }
    return nullptr;
}

bool MainWindow::hasCompassOrientationSource() const
{
    return storedOrientationRecordSet() != nullptr
        || (log_.hasCompassCalibration && recordSetById("compass_raw") != nullptr);
}

bool MainWindow::loadCompassSamples(QString &error)
{
    // Fill compass_samples_ with one quaternion sample per row. Each sample's
    // epoch field holds the row time in the log's own domain (epoch seconds,
    // or elapsed seconds for IMUTag), the same coordinate as the plot x axis.
    compass_samples_.clear();

    if (const SensorRecordSet *stored = storedOrientationRecordSet()) {
        if (!orientationRecordHasExpectedColumns(*stored)) {
            error = tr("The %1 record set is missing one or more expected columns.")
                        .arg(stored->label);
            return false;
        }
        const QVector<double> qw = stored->columns.value("qw");
        const QVector<double> qx = stored->columns.value("qx");
        const QVector<double> qy = stored->columns.value("qy");
        const QVector<double> qz = stored->columns.value("qz");
        const QVector<double> dip = stored->columns.value("dip");
        const QVector<double> field = stored->columns.value("field");
        const QVector<double> acceleration = stored->columns.value("acceleration");
        compass_samples_.reserve(stored->time.size());
        for (qsizetype i = 0; i < stored->time.size(); i++) {
            CompassDerivedSample sample;
            sample.epoch = stored->time[i];
            sample.q = QQuaternion(qw[i], qx[i], qy[i], qz[i]);
            sample.dip = dip[i];
            sample.field = field[i];
            sample.mg = acceleration[i];
            compass_samples_.append(sample);
        }
        compass_source_ = tr("stored quaternions (%1)").arg(stored->label);
        return true;
    }

    const SensorRecordSet *compass = recordSetById("compass_raw");
    if (!compass || !log_.hasCompassCalibration) {
        error = tr("Compass orientation, or compass raw samples and calibration "
                   "constants, are required.");
        return false;
    }
    if (!compassRecordHasExpectedColumns(*compass)) {
        error = tr("The compass raw record set is missing one or more expected columns.");
        return false;
    }

    const QVector<double> ax = compass->columns.value("ax");
    const QVector<double> ay = compass->columns.value("ay");
    const QVector<double> az = compass->columns.value("az");
    const QVector<double> mx = compass->columns.value("mx");
    const QVector<double> my = compass->columns.value("my");
    const QVector<double> mz = compass->columns.value("mz");

    CompassProcessor processor(log_.compassCalibration);
    compass_samples_.reserve(compass->time.size());
    for (qsizetype i = 0; i < compass->time.size(); i++) {
        CompassRawSample raw;
        raw.epoch = compass->time[i];
        raw.accel = QVector3D(ax[i], ay[i], az[i]);
        raw.mag = QVector3D(mx[i], my[i], mz[i]);
        compass_samples_.append(processor.deriveSample(raw));
    }
    compass_source_ = tr("derived from raw compass samples");
    return true;
}

void MainWindow::createCompassPlotStreams()
{
    // Turn the orientation samples into plot-ready streams. compass_samples_
    // also feeds the QML panel on mouse movement.
    QString error;
    if (!loadCompassSamples(error)) {
        compass_samples_.clear();
        compass_source_.clear();
        QMessageBox::warning(this, tr("Compass Streams"), error);
        return;
    }

    const qsizetype sample_count = compass_samples_.size();
    const QVector<double> compass_time = compassSampleTimes(compass_samples_);
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

    for (const CompassDerivedSample &sample : compass_samples_) {
        const CompassAngles angles = compassAnglesFromQuaternion(sample.q);
        acceleration.append(sample.mg);
        pitch.append(angles.pitch);
        roll.append(angles.roll);
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
            compass_time,
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
            compass_time,
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
            compass_time,
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
            compass_time,
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
            compass_time,
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
            compass_time,
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

    if (!hasStream("compass_heading") || compass_samples_.isEmpty()) {
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
            compassSampleTimes(compass_samples_),
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

    if (!hasStream("compass_heading") || compass_samples_.isEmpty()) {
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
            compassSampleTimes(compass_samples_),
            compassHeadingValues(compass_samples_, declination_degrees_, battery_forward_),
            true,
            {true, 0.0, 360.0}),
        heading_checked);
}
