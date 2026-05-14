#include "mainwindow.h"

#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

#include "../qtfiledialog.h"
#include "sqlite_loader.h"

// This file owns the load workflow:
//
// 1. Ask the user for a SQLite log.
// 2. Convert the file into a SensorLog with SqliteLoader.
// 3. Replace the current stream list and View menu actions.
// 4. Update File Info, tag-dependent menus, and the plot.
//
// If the application opens a file but does not show the expected streams, check
// sqlite_loader.cpp first to see whether the table was converted into a stream,
// then check the default visibility loop in loadLog().
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

    // Core temperature and voltage are useful diagnostic streams, but pressure
    // and activity are the primary view for most sensor logs.
    clearStreamActions();
    for (const SensorStream &stream : streams_) {
        addStreamAction(stream, stream.id != "core_temperature" && stream.id != "voltage");
    }

    // Order matters here: menu visibility depends on streams_, and refreshPlot()
    // depends on the stream actions that were just rebuilt.
    updateMetadata();
    qInfo().noquote() << "Loaded" << path;
    updateTransformActions();
    refreshPlot();
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
    for (auto it = log_.info.cbegin(); it != log_.info.cend(); ++it) {
        // The raw protobuf/config blobs are too large for the summary pane.
        if (it.key() == "config" || it.key() == "info") {
            continue;
        }
        lines << it.key() + ": " + it.value();
    }
    info_->setPlainText(lines.join("\n"));

    status_->setText(tr("%1 streams loaded").arg(streams_.size()));
}
