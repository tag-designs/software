#include "mainwindow.h"

#include <QAction>

// compass_transforms.cpp owns the compass's Qt side. The orientation samples
// and the heading, acceleration, pitch, roll, dip and field streams are made
// by the core's CompassTransform (core/transforms.cpp), from a processed log's
// stored quaternions or CompassTag's raw rows; this file feeds the QML compass
// panel and applies the two display choices, declination and battery
// direction, to the panel and, through the registry, to the heading.

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

} // namespace

void MainWindow::loadCompassPanelSamples()
{
    // The panel takes the core's orientation samples as CompassDerivedSample;
    // it reads only the quaternion, dip, field and acceleration.
    compass_samples_.clear();
    compass_source_.clear();
    const sensorviz::CompassTransform *compass = transforms_.compass();
    if (!compass) {
        return;
    }
    compass_samples_.reserve(static_cast<qsizetype>(compass->samples().size()));
    for (const sensorviz::OrientationSample &sample : compass->samples()) {
        CompassDerivedSample derived;
        derived.epoch = sample.time;
        derived.q = toQQuaternion(sample.q);
        derived.dip = sample.dip;
        derived.field = sample.field;
        derived.mg = sample.mg;
        compass_samples_.append(derived);
    }
    compass_source_ = toQString(compass->source());
    if (!compass_samples_.isEmpty()) {
        compass_display_.showSample(compass_samples_.first());
    }
}

void MainWindow::setDeclination()
{
    // Update the user-selected true-north offset. Only the heading stream and
    // QML display change; the magnetic-frame samples are preserved.
    double declination = 0.0;
    if (askParameter(sensorviz::ViewModel::kDeclination, declination)) {
        applyDeclination(declination);
    }
}

void MainWindow::applyDeclination(double declination_degrees)
{
    setTransformParameter(sensorviz::ViewModel::kDeclination, declination_degrees);
    updateDeclinationActionText(declination_action_, view_.declinationDegrees());
    compass_display_.setDeclination(view_.declinationDegrees());
}

void MainWindow::batteryForwardToggled(bool checked)
{
    // Battery direction is a display convention inherited from compviz. It
    // rotates heading by 180 degrees and tells QML how to orient the tag model.
    setTransformParameter(sensorviz::ViewModel::kBatteryForward, checked ? 1.0 : 0.0);
    compass_display_.setBatteryForward(view_.batteryForward());
}
