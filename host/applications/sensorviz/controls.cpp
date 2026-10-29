/**
 * @file    controls.cpp
 * @brief   sensorViz general actions and shared display helpers that belong
 *          to no single runtime slice.
 *
 * @details Holds the MainWindow members that are cross-cutting rather than
 *          part of streams, transforms, plotting or interaction: the units
 *          label helper, the pressure-to-altitude display transforms, the
 *          About and User Guide actions, graph-title editing, and the
 *          read-only compass calibration constants dialog. The file is kept
 *          small on purpose; new behaviour normally belongs in one of
 *          stream_actions.cpp, transforms.cpp, plotting.cpp or
 *          interaction.cpp (see the list below the includes).
 *
 * @note    The altitude transforms are display-only and use the sea-level
 *          pressure held by the ViewModel; they never alter stored samples.
 */

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
    if (log_.path.empty()) {
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
    if (log_.path.empty()) {
        return;
    }

    view_.setTitleVisible(visible);
    refreshPlot();
}

void MainWindow::showCalibrationConstants()
{
    // Compass calibration is loaded by sensorviz::openLog() and stored as typed
    // SensorLog metadata. This dialog is read-only and shared through sensorui.
    if (!log_.hasCompassCalibration) {
        QMessageBox::information(
            this,
            tr("Calibration Constants"),
            tr("No compass calibration constants are available for this log."));
        return;
    }

    CompassCalibrationDialog dialog(this);
    dialog.setConstants(toCompassCalibration(log_.compassCalibration));
    dialog.exec();
}
