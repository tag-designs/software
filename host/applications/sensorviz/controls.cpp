#include "mainwindow.h"

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
    return stream.units.isEmpty() ? QString() : " " + stream.units;
}

double MainWindow::pressureToAltitude(double pressure_mbar) const
{
    return 44330.0 * (1.0 - std::pow(pressure_mbar / sea_level_pressure_, 1.0 / 5.257));
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this,
        tr("About sensorViz"),
        tr("sensorViz\nSQLite sensor log visualization tool"));
}
