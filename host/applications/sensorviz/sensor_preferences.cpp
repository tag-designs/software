#include "mainwindow.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QSignalBlocker>

#include "qtfiledialog.h"

// This file owns sensorViz viewer preferences. The runtime map is keyed by tag
// type so choices for a BitTag log do not bleed into a CompassTag or PresTag
// log. The JSON file format mirrors that map and stores only overrides from
// defaults, keeping preference files short enough to edit by hand.
//
// The preferences themselves are the core's sensorviz::TagPreferences, applied
// to and captured from the ViewModel and kept, one entry per tag type, in the
// core's sensorviz::PreferenceStore, which also reads and writes the JSON
// (core/preference_store.h). This file chooses the files and handles their
// I/O. Built-in stream display defaults are in core/stream_meta.cpp.
//
// Persisted:
// - stream visibility when it differs from SensorStream::defaultVisible
// - stream colors chosen by the user
// - stream axis sides chosen by the user
// - "Break Lines at Gaps" when turned off
//
// Not persisted:
// - y-axis ranges, because those are analysis/session state
// - sea-level pressure and declination, because they are calculation inputs
// - UTC offset and battery-forward, because they describe the current viewing
//   context rather than a tag-type preference

namespace
{

QString preferencesDirectory(const QString &current_path)
{
    return current_path.isEmpty() ? QDir::homePath() : current_path;
}

} // namespace

QString MainWindow::preferenceKey() const
{
    // Prefer the concrete tag type recorded in the log. profileName is a
    // fallback for older or hand-built logs that omit the raw tag type.
    if (!log_.tagType.empty()) {
        return toQString(log_.tagType);
    }
    return toQString(log_.profileName);
}

void MainWindow::rememberCurrentPreferences()
{
    // Store only viewer choices that are meant to follow this tag type. The
    // ViewModel decides what differs from the defaults; session parameters
    // such as sea-level pressure, declination, battery-forward, or UTC offset
    // are never captured.
    if (suppress_preference_updates_) {
        return;
    }

    const QString key = preferenceKey();
    if (key.isEmpty() || view_.streams().empty()) {
        return;
    }

    preferences_.remember(view_.capturePreferences(key.toStdString()));
}

void MainWindow::applyPreferencesForCurrentTag()
{
    // Rebuild the effective UI state for the current tag type. Missing
    // preferences are treated as "load defaults", which is what lets Load
    // Defaults remove one tag entry and then reuse this function.
    const QString key = preferenceKey();
    if (key.isEmpty() || view_.streams().empty()) {
        return;
    }

    // The gap mode is applied after the visibility redraw, as it always has
    // been: y axes take their range partly from the previous drawing, so the
    // order of redraws shows in the plot.
    const SensorVizPreferences preferences = preferences_.get(key.toStdString());
    SensorVizPreferences first_phase = preferences;
    first_phase.hasBreakLinesAtGapsOverride = true;
    first_phase.breakLinesAtGaps = view_.breakAtGaps();

    suppress_preference_updates_ = true;
    const sensorviz::ViewModel::Changes changes = view_.applyPreferences(first_phase);
    if ((changes & sensorviz::ViewModel::Visibility) != 0) {
        // A visibility change redraws on its own, as it always has.
        streamVisibilityChanged();
    }
    view_.setBreakAtGaps(
        preferences.hasBreakLinesAtGapsOverride ? preferences.breakLinesAtGaps : true);
    suppress_preference_updates_ = false;

    rebuildRangeActions();
    updateColorsAction();
    updateAxisSidesAction();
    refreshPlot();
}

void MainWindow::loadPreferences()
{
    const QString path = HostFileDialog::getOpenFileName(
        this,
        tr("Load sensorViz Preferences"),
        preferencesDirectory(current_path_),
        tr("sensorViz Preferences (*.json);;JSON Files (*.json);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }

    QString error;
    if (!loadPreferencesFromFile(path, error)) {
        QMessageBox::critical(this, tr("Load Preferences Failed"), error);
        return;
    }

    applyPreferencesForCurrentTag();
    qInfo().noquote() << "Loaded sensorViz preferences from" << path;
}

void MainWindow::savePreferences()
{
    rememberCurrentPreferences();

    QString initial_path = preferencesDirectory(current_path_);
    if (!initial_path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        initial_path = QDir(initial_path).filePath(QStringLiteral("sensorviz-preferences.json"));
    }
    QString path = HostFileDialog::getSaveFileName(
        this,
        tr("Save sensorViz Preferences"),
        initial_path,
        tr("sensorViz Preferences (*.json);;JSON Files (*.json);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".json");
    }

    QString error;
    if (!savePreferencesToFile(path, error)) {
        QMessageBox::critical(this, tr("Save Preferences Failed"), error);
        return;
    }

    qInfo().noquote() << "Saved sensorViz preferences to" << path;
}

void MainWindow::loadDefaultPreferences()
{
    const QString key = preferenceKey();
    if (key.isEmpty() || view_.streams().empty()) {
        return;
    }

    preferences_.forget(key.toStdString());
    applyPreferencesForCurrentTag();
    qInfo().noquote() << "Loaded default sensorViz preferences for" << key;
}

bool MainWindow::loadPreferencesFromFile(const QString &path, QString &error)
{
    // Preference files replace the in-memory preference map. That keeps the
    // operation predictable: Load means "use exactly this preference file" and
    // Store writes the currently remembered override set back out.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = tr("Could not open %1: %2").arg(path, file.errorString());
        return false;
    }

    std::string load_error;
    if (!preferences_.load(file.readAll().toStdString(), path.toStdString(), load_error)) {
        error = toQString(load_error);
        return false;
    }
    return true;
}

bool MainWindow::savePreferencesToFile(const QString &path, QString &error)
{
    // Write stable, hand-editable JSON; the store holds only entries that
    // still contain at least one override.
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = tr("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }

    file.write(QByteArray::fromStdString(preferences_.save()));
    if (file.error() != QFileDevice::NoError) {
        error = tr("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}
