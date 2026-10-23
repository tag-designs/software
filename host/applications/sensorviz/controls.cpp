#include "mainwindow.h"

#include "compass_calibration_dialog.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>

#include "qtuserguide.h"

#include <cmath>

// This file intentionally stays small. It contains only cross-cutting helpers
// and general actions that do not belong to one of the runtime slices:
//
// - stream_actions.cpp: stream visibility and y-axis range actions
// - transforms.cpp: derived display streams such as altitude and activity filter
// - plotting.cpp: plot state to PlotSpec to PlotView (plot_spec.*, plot_view.*)
// - interaction.cpp: cursor, context-menu, print, UTC, and mouse readout commands

QString MainWindow::unitsSuffix(const SensorStream &stream) const
{
    // Shared label helper used by menus, axes, range dialogs, and tooltips.
    return stream.units.empty() ? QString() : " " + toQString(stream.units);
}

double MainWindow::pressureToAltitude(double pressure_mbar) const
{
    // Display transform for pressure-derived altitude. The sea-level pressure is
    // configured from the Altitude action in transforms.cpp.
    return 44330.0 * (1.0 - std::pow(pressure_mbar / view_.seaLevelPressure(), 1.0 / 5.257));
}

double MainWindow::pressureToAltitude(double pressure_mbar, double temperature_c) const
{
    // Temperature-aware hypsometric display transform. Pressure tags report
    // ambient pressure-sensor temperature as "sensor_temperature"; core
    // temperature is intentionally not used for altitude. The pressure-only
    // overload remains the fallback when no ambient temperature stream exists.
    const double temperature_k = temperature_c + 273.15;
    return (std::pow(view_.seaLevelPressure() / pressure_mbar, 1.0 / 5.257) - 1.0)
        * temperature_k
        / 0.0065;
}

void MainWindow::showAbout()
{
    // Keep the About action here with other small general-purpose actions.
    QMessageBox::about(
        this,
        tr("About sensorViz"),
        tr("sensorViz\nSQLite sensor log visualization tool"));
}

void MainWindow::showUserGuide()
{
    HostUserGuide::open(this, tr("SensorViz User Guide"), QStringLiteral("sensorviz"));
}

void MainWindow::editGraphTitle()
{
    if (log_.path.isEmpty()) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Graph Title"));

    QLineEdit *title_edit = new QLineEdit(toQString(view_.title()), &dialog);
    QCheckBox *show_title = new QCheckBox(tr("Show title"), &dialog);
    show_title->setChecked(view_.titleVisible());
    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(title_edit);
    layout->addWidget(show_title);
    layout->addWidget(buttons);
    dialog.setLayout(layout);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    view_.setTitle(title_edit->text().toStdString());
    view_.setTitleVisible(show_title->isChecked());
    // The title is part of the plot spec; a title-only change is applied
    // without rebuilding the graphs.
    refreshPlot();
}

void MainWindow::setGraphTitleVisible(bool visible)
{
    if (log_.path.isEmpty()) {
        return;
    }

    view_.setTitleVisible(visible);
    refreshPlot();
}

void MainWindow::showCalibrationConstants()
{
    // Compass calibration is loaded in sqlite_loader.cpp and stored as typed
    // SensorLog metadata. This dialog is read-only and shared through sensorui.
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
