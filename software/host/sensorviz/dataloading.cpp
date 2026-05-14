#include "mainwindow.h"

#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

#include "../qtfiledialog.h"
#include "sqlite_loader.h"

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

    clearStreamActions();
    for (const SensorStream &stream : streams_) {
        addStreamAction(stream, true);
    }

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
        if (it.key() == "config" || it.key() == "info") {
            continue;
        }
        lines << it.key() + ": " + it.value();
    }
    info_->setPlainText(lines.join("\n"));

    status_->setText(tr("%1 streams loaded").arg(streams_.size()));
}
