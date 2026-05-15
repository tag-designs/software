#include "mainwindow.h"

#include "compass_calibration_dialog.h"

#include <QMessageBox>

#include <cmath>

// This file intentionally stays small. It contains only cross-cutting helpers
// and general actions that do not belong to one of the runtime slices:
//
// - stream_actions.cpp: stream visibility and y-axis range actions
// - transforms.cpp: derived display streams such as altitude and activity filter
// - plotting.cpp: QCustomPlot graph/axis rebuilds
// - interaction.cpp: cursor, context-menu, print, UTC, and mouse readout logic

QString MainWindow::unitsSuffix(const SensorStream &stream) const
{
    // Shared label helper used by menus, axes, range dialogs, and tooltips.
    return stream.units.isEmpty() ? QString() : " " + stream.units;
}

double MainWindow::pressureToAltitude(double pressure_mbar) const
{
    // Display transform for pressure-derived altitude. sea_level_pressure_ is
    // configured from the Altitude action in transforms.cpp.
    return 44330.0 * (1.0 - std::pow(pressure_mbar / sea_level_pressure_, 1.0 / 5.257));
}

void MainWindow::showAbout()
{
    // Keep the About action here with other small general-purpose actions.
    QMessageBox::about(
        this,
        tr("About sensorViz"),
        tr("sensorViz\nSQLite sensor log visualization tool"));
}

void MainWindow::showCalibrationConstants()
{
    // Compass calibration is loaded in sqlite_loader.cpp and stored as typed
    // SensorLog metadata. This dialog is read-only and shared with compviz.
    if (!log_.hasCompassCalibration) {
        QMessageBox::information(
            this,
            tr("Calibration Constants"),
            tr("No compass calibration constants are available for this log."));
        return;
    }

    CompassCalibrationDialog dialog(this);
    dialog.setConstants(log_.compassCalibration);
    dialog.exec();
}
