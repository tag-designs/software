#include "mainwindow.h"

#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

#include "qtfiledialog.h"
#include "sqlite_loader.h"

// This file owns the load workflow:
//
// 1. Ask the user for a SQLite log.
// 2. Convert the file into a SensorLog with SqliteLoader.
// 3. Replace the current stream list and View menu actions.
// 4. Update File Info, tag-dependent menus, and the plot.
//
// If the application opens a file but does not show the expected streams, check
// sensorprofile.cpp first to see whether the table has a definition, then check
// sqlite_loader.cpp to see whether that definition was loaded successfully.
void MainWindow::loadLog()
{
    const QString path = HostFileDialog::getOpenFileName(
        this,
        tr("Open Sensor Log"),
        current_path_.isEmpty() ? QDir::homePath() : current_path_,
        tr("SQLite Logs (*.db3);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }

    SensorLog log;
    QString error;
    if (!SqliteLoader::load(path, log, error)) {
        QMessageBox::critical(this, tr("Load Failed"), error);
        return;
    }

    current_path_ = QFileInfo(path).absolutePath();
    log_ = log;
    streams_ = log.streams;
    custom_axis_ranges_.clear();
    explicit_axis_ranges_.clear();

    // Default visibility comes from sensorprofile.cpp. The QAction state is the
    // runtime source of truth after this point.
    clearStreamActions();
    for (const SensorStream &stream : streams_) {
        addStreamAction(stream, stream.defaultVisible);
    }

    // Order matters here: menu visibility depends on streams_, and refreshPlot()
    // depends on the stream actions that were just rebuilt.
    updateMetadata();
    calibration_constants_action_->setVisible(log_.hasCompassCalibration);
    calibration_constants_action_->setEnabled(log_.hasCompassCalibration);
    qInfo().noquote() << "Loaded" << path;
    updateTransformActions();
    refreshPlotFullRange();
}

void MainWindow::updateMetadata()
{
    info_->clear();
    if (log_.path.isEmpty()) {
        status_->setText(tr("No log loaded"));
        return;
    }

    QStringList lines;
    lines << QFileInfo(log_.path).fileName();
    if (!log_.tagType.isEmpty()) {
        lines << "Tag type: " + log_.tagType;
    }
    if (!log_.profileName.isEmpty()) {
        lines << "Profile: " + log_.profileName;
    }
    if (!log_.recordSets.isEmpty()) {
        QStringList record_sets;
        for (const SensorRecordSet &record_set : log_.recordSets) {
            record_sets << record_set.label;
        }
        lines << "Record sets: " + record_sets.join(", ");
    }
    if (log_.hasCompassCalibration) {
        lines << "Compass calibration: loaded";
        lines << QString("Compass calibration epoch: %1").arg(log_.compassCalibrationEpoch);
    } else if (!log_.compassCalibrationWarning.isEmpty()) {
        lines << "Compass calibration: " + log_.compassCalibrationWarning;
    }
    for (auto it = log_.info.cbegin(); it != log_.info.cend(); ++it) {
        // The raw protobuf/config blobs are too large for the summary pane.
        if (it.key() == "config" || it.key() == "info") {
            continue;
        }
        lines << it.key() + ": " + it.value();
    }
    info_->setPlainText(lines.join("\n"));

    if (log_.recordSets.isEmpty()) {
        status_->setText(tr("%1 streams loaded").arg(streams_.size()));
    } else {
        status_->setText(tr("%1 streams, %2 record sets loaded")
                             .arg(streams_.size())
                             .arg(log_.recordSets.size()));
    }
}
