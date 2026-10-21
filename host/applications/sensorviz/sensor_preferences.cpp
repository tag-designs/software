#include "mainwindow.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QSignalBlocker>

#include "qtfiledialog.h"

// This file owns sensorViz viewer preferences. The runtime map is keyed by tag
// type so choices for a BitTag log do not bleed into a CompassTag or PresTag
// log. The JSON file format mirrors that map and stores only overrides from
// defaults, keeping preference files short enough to edit by hand.
//
// The preferences themselves are the core's sensorviz::TagPreferences, applied
// to and captured from the ViewModel; this file reads and writes them as JSON
// and keeps one entry per tag type. Built-in stream display defaults are in
// core/stream_meta.cpp.
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

constexpr int kPreferencesVersion = 1;

// ---------------------------------------------------------------------------
// Preference Helpers
// ---------------------------------------------------------------------------

QString axisSideToString(SensorAxisSide side)
{
    return side == SensorAxisSide::Right ? QStringLiteral("right") : QStringLiteral("left");
}

bool axisSideFromString(const QString &text, SensorAxisSide &side)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("left")) {
        side = SensorAxisSide::Left;
        return true;
    }
    if (normalized == QStringLiteral("right")) {
        side = SensorAxisSide::Right;
        return true;
    }
    return false;
}

QJsonObject preferencesToJson(const SensorVizPreferences &preferences)
{
    QJsonObject object;
    if (preferences.hasVisibleStreamOverride) {
        // std::set keeps the ids sorted, as the file always has.
        QJsonArray visible;
        for (const std::string &id : preferences.visibleStreamIds) {
            visible.append(toQString(id));
        }
        object.insert(QStringLiteral("visible_streams"), visible);
    }

    if (!preferences.streamColors.empty()) {
        QJsonObject colors;
        for (const auto &entry : preferences.streamColors) {
            colors.insert(toQString(entry.first), toQColor(entry.second).name(QColor::HexRgb));
        }
        object.insert(QStringLiteral("colors"), colors);
    }

    if (!preferences.axisSides.empty()) {
        QJsonObject axis_sides;
        for (const auto &entry : preferences.axisSides) {
            axis_sides.insert(toQString(entry.first), axisSideToString(entry.second));
        }
        object.insert(QStringLiteral("axis_sides"), axis_sides);
    }

    if (preferences.hasBreakLinesAtGapsOverride) {
        object.insert(QStringLiteral("break_lines_at_gaps"), preferences.breakLinesAtGaps);
    }

    return object;
}

bool jsonToPreferences(
    const QString &tag_type,
    const QJsonObject &object,
    SensorVizPreferences &preferences,
    QString &error)
{
    // Convert one tag entry from the JSON file into the same sparse override
    // structure used at runtime. Unknown stream ids are allowed here because a
    // preference file may be loaded before its matching tag type is opened; ids
    // are filtered against actual loaded streams when preferences are applied.
    preferences = SensorVizPreferences();
    preferences.tagType = tag_type.toStdString();

    if (object.contains(QStringLiteral("visible_streams"))) {
        const QJsonValue value = object.value(QStringLiteral("visible_streams"));
        if (!value.isArray()) {
            error = QStringLiteral("visible_streams for %1 must be an array").arg(tag_type);
            return false;
        }
        preferences.hasVisibleStreamOverride = true;
        const QJsonArray visible = value.toArray();
        for (const QJsonValue &entry : visible) {
            if (!entry.isString()) {
                error = QStringLiteral("visible_streams for %1 must contain only strings")
                            .arg(tag_type);
                return false;
            }
            preferences.visibleStreamIds.insert(entry.toString().toStdString());
        }
    }

    if (object.contains(QStringLiteral("colors"))) {
        const QJsonValue value = object.value(QStringLiteral("colors"));
        if (!value.isObject()) {
            error = QStringLiteral("colors for %1 must be an object").arg(tag_type);
            return false;
        }
        const QJsonObject colors = value.toObject();
        for (auto it = colors.constBegin(); it != colors.constEnd(); ++it) {
            if (!it.value().isString()) {
                error = QStringLiteral("colors.%1 for %2 must be a color string")
                            .arg(it.key(), tag_type);
                return false;
            }
            const QColor color(it.value().toString());
            if (!color.isValid()) {
                error = QStringLiteral("colors.%1 for %2 is not a valid color")
                            .arg(it.key(), tag_type);
                return false;
            }
            preferences.streamColors[it.key().toStdString()] = toRgb(color);
        }
    }

    if (object.contains(QStringLiteral("axis_sides"))) {
        const QJsonValue value = object.value(QStringLiteral("axis_sides"));
        if (!value.isObject()) {
            error = QStringLiteral("axis_sides for %1 must be an object").arg(tag_type);
            return false;
        }
        const QJsonObject axis_sides = value.toObject();
        for (auto it = axis_sides.constBegin(); it != axis_sides.constEnd(); ++it) {
            if (!it.value().isString()) {
                error = QStringLiteral("axis_sides.%1 for %2 must be left or right")
                            .arg(it.key(), tag_type);
                return false;
            }
            SensorAxisSide side = SensorAxisSide::Left;
            if (!axisSideFromString(it.value().toString(), side)) {
                error = QStringLiteral("axis_sides.%1 for %2 must be left or right")
                            .arg(it.key(), tag_type);
                return false;
            }
            preferences.axisSides[it.key().toStdString()] = side;
        }
    }

    if (object.contains(QStringLiteral("break_lines_at_gaps"))) {
        const QJsonValue value = object.value(QStringLiteral("break_lines_at_gaps"));
        if (!value.isBool()) {
            error = QStringLiteral("break_lines_at_gaps for %1 must be true or false")
                        .arg(tag_type);
            return false;
        }
        preferences.hasBreakLinesAtGapsOverride = true;
        preferences.breakLinesAtGaps = value.toBool();
    }

    return true;
}

QString preferencesDirectory(const QString &current_path)
{
    return current_path.isEmpty() ? QDir::homePath() : current_path;
}

} // namespace

QString MainWindow::preferenceKey() const
{
    // Prefer the concrete tag type recorded in the log. profileName is a
    // fallback for older or hand-built logs that omit the raw tag type.
    if (!log_.tagType.isEmpty()) {
        return log_.tagType;
    }
    return log_.profileName;
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

    const SensorVizPreferences preferences = view_.capturePreferences(key.toStdString());
    if (preferences.hasOverrides()) {
        preferences_by_tag_type_[key] = preferences;
    } else {
        preferences_by_tag_type_.remove(key);
    }
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
    const SensorVizPreferences preferences = preferences_by_tag_type_.value(key);
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

    preferences_by_tag_type_.remove(key);
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

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        error = tr("Could not parse %1: %2 at offset %3")
                    .arg(path, parse_error.errorString())
                    .arg(parse_error.offset);
        return false;
    }
    if (!document.isObject()) {
        error = tr("Preference file %1 must contain a JSON object").arg(path);
        return false;
    }

    const QJsonObject root = document.object();
    if (root.contains(QStringLiteral("version"))
        && root.value(QStringLiteral("version")).toInt() != kPreferencesVersion) {
        error = tr("Unsupported sensorViz preferences version in %1").arg(path);
        return false;
    }

    const QJsonValue tags_value = root.value(QStringLiteral("tags"));
    if (!tags_value.isObject()) {
        error = tr("Preference file %1 must contain a tags object").arg(path);
        return false;
    }

    QMap<QString, SensorVizPreferences> loaded_preferences;
    const QJsonObject tags = tags_value.toObject();
    for (auto it = tags.constBegin(); it != tags.constEnd(); ++it) {
        if (!it.value().isObject()) {
            error = tr("Preferences for %1 must be a JSON object").arg(it.key());
            return false;
        }

        SensorVizPreferences preferences;
        if (!jsonToPreferences(it.key(), it.value().toObject(), preferences, error)) {
            return false;
        }
        if (preferences.hasOverrides()) {
            loaded_preferences.insert(it.key(), preferences);
        }
    }

    preferences_by_tag_type_ = loaded_preferences;
    return true;
}

bool MainWindow::savePreferencesToFile(const QString &path, QString &error)
{
    // Write stable, hand-editable JSON. The per-tag entries are already sparse;
    // this function only emits entries that still contain at least one override.
    QJsonObject tags;
    for (auto it = preferences_by_tag_type_.cbegin(); it != preferences_by_tag_type_.cend(); ++it) {
        if (it.value().hasOverrides()) {
            tags.insert(it.key(), preferencesToJson(it.value()));
        }
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kPreferencesVersion);
    root.insert(QStringLiteral("tags"), tags);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = tr("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (file.error() != QFileDevice::NoError) {
        error = tr("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}
