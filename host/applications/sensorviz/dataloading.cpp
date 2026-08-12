#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>

#include "qtfiledialog.h"
#include "sqlite_loader.h"

namespace
{

struct LoadResult
{
    bool ok = false;
    SensorLog log;
    QString error;
};

LoadResult loadSensorLog(const QString &path)
{
    LoadResult result;
    result.ok = SqliteLoader::load(path, result.log, result.error);
    return result;
}

} // namespace

// This file owns the load workflow:
//
// 1. Ask the user for a SQLite log.
// 2. Convert the file into a SensorLog with SqliteLoader.
// 3. Replace the current stream list and View menu actions.
// 4. Update File Info, tag-dependent menus, and the plot.
//
// If the application opens a file but does not show the expected streams, check
// the database streams table first, then sqlite_loader.cpp to see whether that
// metadata was loaded successfully.
void MainWindow::loadLog()
{
    if (log_load_in_progress_) {
        return;
    }

    // This is the only file-open path. It asks SqliteLoader for a fresh
    // SensorLog, replaces all stream/action state, and then lets transform
    // actions auto-enable any derived streams that should exist on load.
    const QString path = HostFileDialog::getOpenFileName(
        this,
        tr("Open Sensor Log"),
        current_path_.isEmpty() ? QDir::homePath() : current_path_,
        tr("SQLite Logs (*.db3);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }

    setLogLoadInProgress(true, path);
    auto *watcher = new QFutureWatcher<LoadResult>(this);
    connect(watcher, &QFutureWatcher<LoadResult>::finished, this, [this, watcher, path]() {
        const LoadResult result = watcher->result();
        watcher->deleteLater();

        if (!result.ok) {
            setLogLoadInProgress(false);
            QMessageBox::critical(this, tr("Load Failed"), result.error);
            updateMetadata();
            return;
        }

        status_->setText(tr("Preparing plot for %1...").arg(QFileInfo(path).fileName()));
        applyLoadedLog(path, result.log);
        setLogLoadInProgress(false);
    });
    watcher->setFuture(QtConcurrent::run(loadSensorLog, path));
}

void MainWindow::setLogLoadInProgress(bool loading, const QString &path)
{
    if (log_load_in_progress_ == loading) {
        return;
    }

    log_load_in_progress_ = loading;
    load_action_->setEnabled(!loading);
    if (loading) {
        QApplication::setOverrideCursor(Qt::BusyCursor);
        status_->setText(tr("Loading %1...").arg(QFileInfo(path).fileName()));
    } else {
        QApplication::restoreOverrideCursor();
    }
}

void MainWindow::applyLoadedLog(const QString &path, const SensorLog &log)
{
    rememberCurrentPreferences();

    current_path_ = QFileInfo(path).absolutePath();
    graph_title_ = QFileInfo(path).fileName();
    graph_title_visible_ = true;
    show_title_action_->setChecked(true);
    log_ = log;
    updateLoadedStateActions();
    streams_ = log.streams;
    compass_samples_.clear();
    custom_axis_ranges_.clear();
    explicit_axis_ranges_.clear();
    custom_stream_colors_.clear();
    custom_axis_sides_.clear();

    // Default visibility comes from sensorViz display defaults applied by the
    // SQLite loader. The QAction state is the runtime source of truth after
    // this point.
    clearStreamActions();
    for (const SensorStream &stream : streams_) {
        addStreamAction(stream, stream.defaultVisible);
    }
    updateColorsAction();

    calibration_constants_action_->setVisible(log_.hasCompassCalibration);
    calibration_constants_action_->setEnabled(log_.hasCompassCalibration);
    qInfo().noquote() << "Loaded" << path;
    updateTransformActions();

    // Altitude is a generated stream controlled from Visible Streams; mean
    // sea-level pressure is the Configuration value that changes its samples.
    if (hasStream("pressure") || hasStream("imu_pressure")) {
        createAltitudeStream();
    }

    // CompassTag logs are mostly useful once raw compass rows are converted
    // into scalar streams. Generate that family automatically; the resulting
    // streams are ordinary entries in the Visible Streams dialog.
    if (log_.hasCompassCalibration && recordSetById("compass_raw")) {
        createCompassPlotStreams();
    }
    createImuMagnitudeStreams();

    applyPreferencesForCurrentTag();

    // Order matters here: menu visibility depends on streams_, and refreshPlot()
    // depends on the stream actions that were just rebuilt.
    updateTimeAxisForLog();
    updateMetadata();
    refreshPlotFullRange();
}

void MainWindow::updateMetadata()
{
    // Rebuild the File Info tab and status bar from the current SensorLog.
    // Large protobuf/config blobs stay in log_.info but are omitted here so the
    // pane remains readable and useful for bug reports.
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
        if (!log_.compassCalibrationWarning.isEmpty()) {
            lines << "Compass calibration warning: " + log_.compassCalibrationWarning;
        }
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
