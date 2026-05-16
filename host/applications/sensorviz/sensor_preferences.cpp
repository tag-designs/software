#include "mainwindow.h"

#include <QAction>

// This file owns in-memory sensorViz preferences. Preferences are keyed by tag
// type and survive loading another log during the same run. File persistence is
// intentionally left for a later step; session calculation parameters such as
// sea-level pressure and declination stay on MainWindow and are not captured.

namespace
{

QSet<QString> currentStreamIds(const QVector<SensorStream> &streams)
{
    QSet<QString> ids;
    for (const SensorStream &stream : streams) {
        ids.insert(stream.id);
    }
    return ids;
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
    for (QAction *action : stream_actions_) {
        if (action->isChecked()) {
            preferences.visibleStreamIds.insert(action->data().toString());
        }
    }

    const QSet<QString> stream_ids = currentStreamIds(streams_);
    for (auto it = custom_stream_colors_.cbegin(); it != custom_stream_colors_.cend(); ++it) {
        if (stream_ids.contains(it.key())) {
            preferences.streamColors[it.key()] = it.value();
        }
    }
    for (auto it = custom_axis_sides_.cbegin(); it != custom_axis_sides_.cend(); ++it) {
        const SensorStream *stream = streamById(it.key());
        if (stream && it.value() != stream->axisSide) {
            preferences.axisSides[it.key()] = it.value();
        }
    }

    preferences_by_tag_type_[key] = preferences;
}

void MainWindow::applyPreferencesForCurrentTag()
{
    const QString key = preferenceKey();
    if (key.isEmpty() || !preferences_by_tag_type_.contains(key)) {
        return;
    }

    const SensorVizPreferences preferences = preferences_by_tag_type_[key];
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

    applyStreamVisibility(preferences.visibleStreamIds);
    suppress_preference_updates_ = false;

    rebuildRangeActions();
    updateColorsAction();
    updateAxisSidesAction();
    refreshPlot();
}
