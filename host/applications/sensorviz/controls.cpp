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

#include <cmath>

namespace {

QStringList sensorVizDocsPages()
{
    return {
        QStringLiteral("apps/sensorviz.html"),
        QStringLiteral("apps/sensorviz/index.html"),
    };
}

QStringList sensorVizDocumentationRoots()
{
    QStringList roots;
    QDir probe_dir(QCoreApplication::applicationDirPath());

    for (int level = 0; level < 6; ++level) {
        roots << probe_dir.filePath(QStringLiteral("docs"));
        roots << probe_dir.filePath(QStringLiteral("host/docs/site"));
        roots << probe_dir.filePath(QStringLiteral("share/UltralightTags/docs"));
        if (!probe_dir.cdUp()) {
            break;
        }
    }

#ifdef SENSORVIZ_DOCS_BUILD_DIR
    roots << QStringLiteral(SENSORVIZ_DOCS_BUILD_DIR);
#endif

#ifdef SENSORVIZ_DOCS_SOURCE_DIR
    roots << QDir(QStringLiteral(SENSORVIZ_DOCS_SOURCE_DIR))
                 .filePath(QStringLiteral("site"));
#endif

    QStringList unique_roots;
    for (const QString &root : roots) {
        const QString clean_root = QDir::cleanPath(root);
        if (!clean_root.isEmpty()
            && clean_root != QStringLiteral(".")
            && !unique_roots.contains(clean_root)) {
            unique_roots << clean_root;
        }
    }
    return unique_roots;
}

} // namespace

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

void MainWindow::updateGraphTitle()
{
    // The graph title is view state for the currently loaded file. It is not a
    // persisted preference because users often want different titles for
    // different exports of the same tag type.
    if (!plot_title_) {
        return;
    }

    const bool show_title = graph_title_visible_ && !graph_title_.isEmpty();
    plot_title_->setText(show_title ? graph_title_ : QString());
    plot_title_->setVisible(show_title);
}

double MainWindow::pressureToAltitude(double pressure_mbar) const
{
    // Display transform for pressure-derived altitude. sea_level_pressure_ is
    // configured from the Altitude action in transforms.cpp.
    return 44330.0 * (1.0 - std::pow(pressure_mbar / sea_level_pressure_, 1.0 / 5.257));
}

double MainWindow::pressureToAltitude(double pressure_mbar, double temperature_c) const
{
    // Temperature-aware hypsometric display transform. Pressure tags report
    // ambient pressure-sensor temperature as "sensor_temperature"; core
    // temperature is intentionally not used for altitude. The pressure-only
    // overload remains the fallback when no ambient temperature stream exists.
    const double temperature_k = temperature_c + 273.15;
    return (std::pow(sea_level_pressure_ / pressure_mbar, 1.0 / 5.257) - 1.0)
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
    const QStringList docs_pages = sensorVizDocsPages();
    const QStringList docs_roots = sensorVizDocumentationRoots();

    for (const QString &root : docs_roots) {
        for (const QString &docs_page : docs_pages) {
            const QString path = QDir(root).filePath(docs_page);
            if (QFileInfo::exists(path)) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(path));
                return;
            }
        }
    }

    QMessageBox message(this);
    message.setIcon(QMessageBox::Information);
    message.setWindowTitle(tr("SensorViz User Guide"));
    message.setText(tr("The SensorViz user guide was not found."));
    message.setInformativeText(
        tr("Build the host documentation target and try again:\n"
           "cmake --build <build-dir> --target docs"));
    message.setDetailedText(
        tr("Looked in:\n%1").arg(docs_roots.join(QStringLiteral("\n"))));
    message.exec();
}

void MainWindow::editGraphTitle()
{
    if (log_.path.isEmpty()) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Graph Title"));

    QLineEdit *title_edit = new QLineEdit(graph_title_, &dialog);
    QCheckBox *show_title = new QCheckBox(tr("Show title"), &dialog);
    show_title->setChecked(graph_title_visible_);
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

    graph_title_ = title_edit->text();
    graph_title_visible_ = show_title->isChecked();
    {
        const QSignalBlocker blocker(show_title_action_);
        show_title_action_->setChecked(graph_title_visible_);
    }
    updateGraphTitle();
    plot_->replot();
}

void MainWindow::setGraphTitleVisible(bool visible)
{
    if (log_.path.isEmpty()) {
        return;
    }

    graph_title_visible_ = visible;
    updateGraphTitle();
    plot_->replot();
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
