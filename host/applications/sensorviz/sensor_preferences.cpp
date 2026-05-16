#include "mainwindow.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>

#include "qtfiledialog.h"

// This file owns sensorViz viewer preferences. The runtime map is keyed by tag
// type so choices for a BitTag log do not bleed into a CompassTag or PresTag
// log. The JSON file format mirrors that map and stores only overrides from
// defaults, keeping preference files short enough to edit by hand.
//
// Stream display defaults are also kept here. The SQLite loader reads id/name/
// units/sample data from logs, then asks this module how sensorViz should draw
// each stream by default. Keeping defaults and overrides together gives one
// place to edit display policy.
//
// Persisted:
// - stream visibility when it differs from SensorStream::defaultVisible
// - stream colors chosen by the user
// - stream axis sides chosen by the user
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
// Stream Display Defaults
// ---------------------------------------------------------------------------
//
// Add new stored or derived stream ids here when they need a specific initial
// color, visibility state, axis side, or fixed y-axis range. These defaults are
// sensorViz display policy; saved preference files contain only user overrides
// from this table.

} // namespace

SensorStreamDisplayDefaults defaultDisplayForStream(const QString &stream_id)
{
    // These are viewer presentation choices, not part of the SQLite scientific
    // data contract. Keep them keyed by stable stream id so future viewers can
    // choose their own colors/ranges while tagcore logs remain tool-neutral.
    if (stream_id == "activity") {
        return {QColor(30, 90, 180), true, SensorAxisSide::Left, {true, 0.0, 100.0}};
    }
    if (stream_id == "pressure") {
        return {QColor(25, 130, 105), true, SensorAxisSide::Left, {}};
    }
    if (stream_id == "sensor_temperature") {
        return {QColor(210, 95, 35), true, SensorAxisSide::Left, {}};
    }
    if (stream_id == "core_temperature") {
        return {QColor(180, 55, 55), false, SensorAxisSide::Right, {true, 0.0, 50.0}};
    }
    if (stream_id == "voltage") {
        return {QColor(60, 145, 75), false, SensorAxisSide::Right, {true, 0.0, 5.0}};
    }
    return {};
}

namespace
{

// ---------------------------------------------------------------------------
// Preference Helpers
// ---------------------------------------------------------------------------

QSet<QString> currentStreamIds(const QVector<SensorStream> &streams)
{
    QSet<QString> ids;
    for (const SensorStream &stream : streams) {
        ids.insert(stream.id);
    }
    return ids;
}

QSet<QString> defaultVisibleStreamIds(const QVector<SensorStream> &streams)
{
    QSet<QString> ids;
    for (const SensorStream &stream : streams) {
        if (stream.defaultVisible) {
            ids.insert(stream.id);
        }
    }
    return ids;
}

bool hasPreferenceOverrides(const SensorVizPreferences &preferences)
{
    return preferences.hasVisibleStreamOverride
        || !preferences.streamColors.isEmpty()
        || !preferences.axisSides.isEmpty();
}

QStringList sortedStrings(const QSet<QString> &values)
{
    QStringList sorted;
    for (const QString &value : values) {
        sorted.append(value);
    }
    sorted.sort();
    return sorted;
}

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
        QJsonArray visible;
        for (const QString &id : sortedStrings(preferences.visibleStreamIds)) {
            visible.append(id);
        }
        object.insert(QStringLiteral("visible_streams"), visible);
    }

    if (!preferences.streamColors.isEmpty()) {
        QJsonObject colors;
        for (auto it = preferences.streamColors.cbegin(); it != preferences.streamColors.cend(); ++it) {
            colors.insert(it.key(), it.value().name(QColor::HexRgb));
        }
        object.insert(QStringLiteral("colors"), colors);
    }

    if (!preferences.axisSides.isEmpty()) {
        QJsonObject axis_sides;
        for (auto it = preferences.axisSides.cbegin(); it != preferences.axisSides.cend(); ++it) {
            axis_sides.insert(it.key(), axisSideToString(it.value()));
        }
        object.insert(QStringLiteral("axis_sides"), axis_sides);
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
    preferences.tagType = tag_type;

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
            preferences.visibleStreamIds.insert(entry.toString());
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
            preferences.streamColors.insert(it.key(), color);
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
            preferences.axisSides.insert(it.key(), side);
        }
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
    // Store only viewer choices that are meant to follow this tag type. Do not
    // capture session parameters such as sea-level pressure, declination,
    // battery-forward, or UTC offset.
    if (suppress_preference_updates_) {
        return;
    }

    const QString key = preferenceKey();
    if (key.isEmpty() || streams_.isEmpty()) {
        return;
    }

    SensorVizPreferences preferences;
    preferences.tagType = key;

    // Store visibility only when it differs from built-in stream defaults. The
    // separate boolean is necessary because an empty set can be a real user
    // choice: all streams hidden.
    QSet<QString> visible_stream_ids;
    for (QAction *action : stream_actions_) {
        if (action->isChecked()) {
            visible_stream_ids.insert(action->data().toString());
        }
    }
    const QSet<QString> default_visible_ids = defaultVisibleStreamIds(streams_);
    if (visible_stream_ids != default_visible_ids) {
        preferences.hasVisibleStreamOverride = true;
        preferences.visibleStreamIds = visible_stream_ids;
    }

    const QSet<QString> stream_ids = currentStreamIds(streams_);
    for (auto it = custom_stream_colors_.cbegin(); it != custom_stream_colors_.cend(); ++it) {
        const SensorStream *stream = streamById(it.key());
        if (stream_ids.contains(it.key()) && stream && it.value() != stream->color) {
            preferences.streamColors[it.key()] = it.value();
        }
    }
    for (auto it = custom_axis_sides_.cbegin(); it != custom_axis_sides_.cend(); ++it) {
        const SensorStream *stream = streamById(it.key());
        if (stream && it.value() != stream->axisSide) {
            preferences.axisSides[it.key()] = it.value();
        }
    }

    if (hasPreferenceOverrides(preferences)) {
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
    if (key.isEmpty() || streams_.isEmpty()) {
        return;
    }

    const SensorVizPreferences preferences = preferences_by_tag_type_.value(key);
    const QSet<QString> stream_ids = currentStreamIds(streams_);

    suppress_preference_updates_ = true;
    custom_stream_colors_.clear();
    for (auto it = preferences.streamColors.cbegin(); it != preferences.streamColors.cend(); ++it) {
        if (stream_ids.contains(it.key())) {
            custom_stream_colors_[it.key()] = it.value();
        }
    }

    custom_axis_sides_.clear();
    for (auto it = preferences.axisSides.cbegin(); it != preferences.axisSides.cend(); ++it) {
        if (stream_ids.contains(it.key())) {
            custom_axis_sides_[it.key()] = it.value();
        }
    }

    if (preferences.hasVisibleStreamOverride) {
        applyStreamVisibility(preferences.visibleStreamIds);
    } else {
        applyStreamVisibility(defaultVisibleStreamIds(streams_));
    }
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
    if (key.isEmpty() || streams_.isEmpty()) {
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
        if (hasPreferenceOverrides(preferences)) {
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
        if (hasPreferenceOverrides(it.value())) {
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
