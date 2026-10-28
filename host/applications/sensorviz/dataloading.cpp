#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>

#include "qtfiledialog.h"
#include "sensorstream.h"

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
    std::string error;
    result.ok = sensorviz::openLog(path.toStdString(), result.log, error);
    result.error = toQString(error);
    return result;
}

} // namespace

// This file owns the load workflow:
//
// 1. Ask the user for a SQLite log.
// 2. Read the file into a SensorLog with sensorviz::openLog().
// 3. Replace the current stream list and View menu actions.
// 4. Update File Info, tag-dependent menus, and the plot.
//
// If the application opens a file but does not show the expected streams, check
// the database streams table first, then core/stream_catalog.cpp to see whether that
// metadata was loaded successfully.
void MainWindow::loadLog()
{
    if (log_load_in_progress_) {
        return;
    }

    // This is the only file-open path. It asks openLog() for a fresh
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

bool MainWindow::loadLogFromPathForDocumentation(const QString &path, QString &error)
{
    if (path.isEmpty()) {
        error = tr("No SQLite log path was provided.");
        return false;
    }
    if (log_load_in_progress_) {
        error = tr("A log load is already in progress.");
        return false;
    }

    setLogLoadInProgress(true, path);
    const LoadResult result = loadSensorLog(path);
    if (!result.ok) {
        setLogLoadInProgress(false);
        error = result.error;
        updateMetadata();
        return false;
    }

    status_->setText(tr("Preparing plot for %1...").arg(QFileInfo(path).fileName()));
    applyLoadedLog(path, result.log);
    setLogLoadInProgress(false);
    return true;
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
    const std::string title = QFileInfo(path).fileName().toStdString();
    view_.setTitle(title);
    if (!view_.titleVisible() && !log_.path.empty()) {
        // Re-showing a hidden title redraws, as toggling Show Graph Title
        // always has; this is still the previous log's plot.
        view_.setTitleVisible(true);
        refreshPlot();
    }
    // The plot's StreamIds name streams of the new log from here on.
    plot_view_->setLog(log.data.get());
    log_ = log;
    updateLoadedStateActions();
    compass_samples_.clear();
    compass_source_.clear();

    // A new log starts from the display defaults: each stream's default
    // visibility, no custom ranges, colors or sides. The ViewModel is the
    // runtime source of truth from here on; the actions follow it.
    view_.resetForLog(log.streams, title);
    clearRangeActions();
    updateColorsAction();
    updateAxisSidesAction();
    updateTransformActions();
    updateColorsAction();

    calibration_constants_action_->setVisible(log_.hasCompassCalibration);
    calibration_constants_action_->setEnabled(log_.hasCompassCalibration);
    qInfo().noquote() << "Loaded" << path;
    updateTransformActions();

    // The display transforms make their streams: altitude (sea-level pressure
    // is the Configuration value that changes it), compass orientation from
    // stored quaternions or raw CompassTag rows, and the IMU magnitudes. Their
    // outputs are ordinary entries in the Visible Streams dialog.
    runTransforms();

    applyPreferencesForCurrentTag();

    // Order matters here: menu visibility depends on the streams, and
    // refreshPlot() on the preferences just applied.
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
    if (log_.path.empty()) {
        status_->setText(tr("No log loaded"));
        return;
    }

    QStringList lines;
    lines << QFileInfo(toQString(log_.path)).fileName();
    if (!log_.tagType.empty()) {
        lines << "Tag type: " + toQString(log_.tagType);
    }
    if (!log_.profileName.empty()) {
        lines << "Profile: " + toQString(log_.profileName);
    }
    if (!log_.recordSets.empty()) {
        QStringList record_sets;
        for (const SensorRecordSet &record_set : log_.recordSets) {
            record_sets << toQString(record_set.label);
        }
        lines << "Record sets: " + record_sets.join(", ");
    }
    if (log_.hasCompassCalibration) {
        lines << "Compass calibration: loaded";
        lines << QString("Compass calibration epoch: %1").arg(log_.compassCalibrationEpoch);
        if (!log_.compassCalibrationWarning.empty()) {
            lines << "Compass calibration warning: " + toQString(log_.compassCalibrationWarning);
        }
    } else if (!log_.compassCalibrationWarning.empty()) {
        lines << "Compass calibration: " + toQString(log_.compassCalibrationWarning);
    }
    if (!compass_source_.isEmpty()) {
        lines << "Compass orientation: " + compass_source_;
    }
    for (const auto &[field, value] : log_.info) {
        // The raw protobuf/config blobs are too large for the summary pane.
        if (field == "config" || field == "info") {
            continue;
        }
        lines << toQString(field) + ": " + toQString(value);
    }
    info_->setPlainText(lines.join("\n"));

    if (log_.recordSets.empty()) {
        status_->setText(tr("%1 streams loaded").arg(view_.streams().size()));
    } else {
        status_->setText(tr("%1 streams, %2 record sets loaded")
                             .arg(view_.streams().size())
                             .arg(static_cast<qsizetype>(log_.recordSets.size())));
    }
}
