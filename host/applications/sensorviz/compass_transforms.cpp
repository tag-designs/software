#include "mainwindow.h"

#include "compass_angles.h"
#include "compass_processor.h"

#include <QAction>
#include <QQuaternion>
#include <QInputDialog>
#include <QMessageBox>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <optional>

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
    bool default_visible,
    const SensorAxisRange &range = {})
{
    // Construct the normal SensorStream objects that plotting, range controls,
    // tooltips, and View > Visible Streams already understand. The caller
    // registers the values as plot data.
    SensorStream stream;
    stream.id = id.toStdString();
    stream.label = label.toStdString();
    stream.units = units.toStdString();
    stream.color = toRgb(color);
    stream.defaultVisible = default_visible;
    stream.axisRange = range;
    return stream;
}

bool recordSetHasColumns(const SensorRecordSet &record_set, std::initializer_list<const char *> columns)
{
    // Validate the record set before reading it. Missing columns usually mean
    // a schema drift in tagcore's streams metadata.
    for (const char *column : columns) {
        if (std::find(record_set.columns.begin(), record_set.columns.end(), column)
            == record_set.columns.end()) {
            return false;
        }
    }
    return true;
}

// Processed logs (dataprocessing processor version 2) carry orientation as a
// record set of quaternion, dip, field and acceleration columns. The CompassTag
// set comes first: a processed CompassTag log also has compass_raw, and the
// stored quaternions are what the file says.
const char *const kStoredOrientationRecordSets[] = {
    "compass_orientation",
    "imu_compass_orientation",
};

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
    // Rows are read as the SQLite doubles, so CompassProcessor sees exactly
    // the inputs it always has.
    compass_samples_.clear();
    compass_time_source_ = sensordata::kNoStream;
    if (!log_.data) {
        error = tr("The log has no sample data.");
        return false;
    }
    std::string read_error;

    if (const SensorRecordSet *stored = storedOrientationRecordSet()) {
        if (!recordSetHasColumns(
                *stored, {"qw", "qx", "qy", "qz", "dip", "field", "acceleration"})) {
            error = tr("The %1 record set is missing one or more expected columns.")
                        .arg(toQString(stored->label));
            return false;
        }
        const bool read = log_.data->forEachRow(
            stored->table,
            {"qw", "qx", "qy", "qz", "dip", "field", "acceleration"},
            [this](double time, const double *v) {
                CompassDerivedSample sample;
                sample.epoch = time;
                sample.q = QQuaternion(v[0], v[1], v[2], v[3]);
                sample.dip = v[4];
                sample.field = v[5];
                sample.mg = v[6];
                compass_samples_.append(sample);
            },
            read_error);
        if (!read) {
            compass_samples_.clear();
            error = QString::fromStdString(read_error);
            return false;
        }
        compass_time_source_ = stored->timeSource;
        compass_source_ = tr("stored quaternions (%1)").arg(toQString(stored->label));
        return true;
    }

    // Live orientation comes only from CompassTag's compass_raw, which holds
    // accelerometer and magnetometer samples taken together. IMUTag's ImuAccel
    // and ImuMag are never paired here; IMUTag orientation is read only from a
    // processed log's stored quaternions.
    const SensorRecordSet *compass = recordSetById("compass_raw");
    if (!compass || !log_.hasCompassCalibration) {
        error = tr("Compass orientation, or compass raw samples and calibration "
                   "constants, are required.");
        return false;
    }
    if (!recordSetHasColumns(*compass, {"ax", "ay", "az", "mx", "my", "mz"})) {
        error = tr("The compass raw record set is missing one or more expected columns.");
        return false;
    }

    CompassProcessor processor(toCompassCalibration(log_.compassCalibration));
    const bool read = log_.data->forEachRow(
        compass->table,
        {"ax", "ay", "az", "mx", "my", "mz"},
        [this, &processor](double time, const double *v) {
            CompassRawSample raw;
            raw.epoch = time;
            raw.accel = QVector3D(v[0], v[1], v[2]);
            raw.mag = QVector3D(v[3], v[4], v[5]);
            compass_samples_.append(processor.deriveSample(raw));
        },
        read_error);
    if (!read) {
        compass_samples_.clear();
        error = QString::fromStdString(read_error);
        return false;
    }
    compass_time_source_ = compass->timeSource;
    compass_source_ = tr("derived from raw compass samples");
    return true;
}

void MainWindow::addCompassStream(SensorStream stream, const QVector<double> &values, bool checked)
{
    // Register one compass view on the record set's time axis (one value per
    // row) and publish it. The previous stream of the same id, if any, is
    // released by addOrReplaceStream().
    if (!log_.data) {
        return;
    }
    stream.timeDomain = log_.timeDomain;
    const std::optional<sensordata::StreamId> id = log_.data->addDerived(
        derivedPlotInfo(stream), compass_time_source_, values.constData(),
        static_cast<std::size_t>(values.size()));
    if (!id) {
        qWarning().noquote() << "Could not register compass stream" << toQString(stream.id);
        return;
    }
    stream.dataId = *id;
    addOrReplaceStream(stream, checked);
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
    const QVector<double> heading =
        compassHeadingValues(compass_samples_, view_.declinationDegrees(), view_.batteryForward());
    if (!compass_samples_.isEmpty()) {
        compass_display_.showSample(compass_samples_.first());
    }

    addCompassStream(
        makeCompassStream("compass_heading", tr("Heading"), tr("deg"), QColor(195, 40, 175),
                          true, {true, 0.0, 360.0}),
        heading, true);
    addCompassStream(
        makeCompassStream("compass_acceleration", tr("Acceleration"), tr("mg"),
                          QColor(85, 110, 210), true, {true, 800.0, 1200.0}),
        acceleration, true);
    addCompassStream(
        makeCompassStream("compass_pitch", tr("Pitch"), tr("deg"), QColor(215, 115, 40), false,
                          {true, -180.0, 180.0}),
        pitch, false);
    addCompassStream(
        makeCompassStream("compass_roll", tr("Roll"), tr("deg"), QColor(115, 85, 180), false,
                          {true, -180.0, 180.0}),
        roll, false);
    addCompassStream(
        makeCompassStream("compass_dip", tr("Dip"), tr("deg"), QColor(25, 150, 155), false,
                          {true, -90.0, 90.0}),
        dip, false);
    addCompassStream(
        makeCompassStream("compass_field", tr("Magnetic field"), tr("uT"), QColor(105, 130, 40),
                          false),
        field, false);
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
        view_.declinationDegrees(),
        -180.0,
        180.0,
        2,
        &ok);
    if (!ok) {
        return;
    }

    view_.setDeclinationDegrees(declination);
    updateDeclinationActionText(declination_action_, view_.declinationDegrees());
    compass_display_.setDeclination(view_.declinationDegrees());

    if (!hasStream("compass_heading") || compass_samples_.isEmpty()) {
        return;
    }

    const bool checked = isStreamVisible("compass_heading");
    addCompassStream(
        makeCompassStream("compass_heading", tr("Heading"), tr("deg"), QColor(195, 40, 175),
                          true, {true, 0.0, 360.0}),
        compassHeadingValues(compass_samples_, view_.declinationDegrees(), view_.batteryForward()),
        checked);
}

void MainWindow::batteryForwardToggled(bool checked)
{
    // Battery direction is a display convention inherited from compviz. It
    // rotates heading by 180 degrees and tells QML how to orient the tag model.
    view_.setBatteryForward(checked);
    compass_display_.setBatteryForward(view_.batteryForward());

    if (!hasStream("compass_heading") || compass_samples_.isEmpty()) {
        return;
    }

    const bool heading_checked = isStreamVisible("compass_heading");
    addCompassStream(
        makeCompassStream("compass_heading", tr("Heading"), tr("deg"), QColor(195, 40, 175),
                          true, {true, 0.0, 360.0}),
        compassHeadingValues(compass_samples_, view_.declinationDegrees(), view_.batteryForward()),
        heading_checked);
}
