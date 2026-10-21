#include "mainwindow.h"

// MainWindow's side of the plot: collect the view's state, build the spec and
// hand it to PlotView. Policy (axes, ranges, metadata rows) is buildPlotSpec()
// in plot_spec.cpp; everything drawn is PlotView's.

PlotState MainWindow::plotState() const
{
    // Visibility still lives in the stream QActions; visibleStreams() reads
    // them in drawing order.
    PlotState state;
    for (const SensorStream &stream : visibleStreams()) {
        state.visibleStreamIds.append(stream.id);
    }
    for (auto it = custom_axis_ranges_.cbegin(); it != custom_axis_ranges_.cend(); ++it) {
        state.customRanges.insert(it.key(), PlotRange{it.value().lower, it.value().upper});
    }
    state.utcOffsetHours = utc_offset_;
    state.title = graph_title_;
    state.titleVisible = graph_title_visible_;
    state.declinationDegrees = declination_degrees_;
    state.seaLevelPressure = sea_level_pressure_;
    return state;
}

PlotDisplayPreferences MainWindow::plotDisplayPreferences() const
{
    PlotDisplayPreferences preferences;
    preferences.colors = custom_stream_colors_;
    preferences.axisSides = custom_axis_sides_;
    return preferences;
}

void MainWindow::refreshPlot()
{
    // Normal redraw path: keep the user's current x-axis window.
    rebuildPlot(false);
}

void MainWindow::refreshPlotFullRange()
{
    // Full-range redraw path used after load and Reset Zoom.
    rebuildPlot(true);
}

void MainWindow::rebuildPlot(bool reset_x_range)
{
    plot_view_->setSpec(
        buildPlotSpec(log_, streams_, plotDisplayPreferences(), plotState()));
    if (reset_x_range) {
        plot_view_->resetZoom();
    }
}

void MainWindow::resetZoom()
{
    // User-visible command: clear all manual y ranges and show the full loaded
    // time span again.
    if (plot_view_->spec().traces.isEmpty()) {
        return;
    }

    // Reset Zoom means "return to the metadata/data defaults", not merely
    // "show the full time window". Clear both manual ranges and linked ranges
    // such as the altitude range derived from pressure.
    custom_axis_ranges_.clear();
    explicit_axis_ranges_.clear();
    refreshPlotFullRange();
}

const sensordata::StreamInfo *MainWindow::plotDataInfo(const SensorStream &stream) const
{
    return log_.data ? log_.data->info(stream.dataId) : nullptr;
}

sensordata::StreamInfo MainWindow::derivedPlotInfo(const SensorStream &stream) const
{
    // Description of a transform output for addDerived()/addComputed(); the
    // backend fills in the time axis and summaries.
    sensordata::StreamInfo info;
    info.id = stream.id.toStdString();
    info.label = stream.label.toStdString();
    info.units = stream.units.toStdString();
    info.kind = "derived";
    info.domain = stream.timeDomain == SensorTimeDomain::ElapsedSeconds
        ? sensordata::TimeDomain::ElapsedSeconds
        : sensordata::TimeDomain::EpochSeconds;
    return info;
}

bool MainWindow::readStreamDoubles(
    const SensorStream &stream,
    std::vector<double> &time,
    std::vector<double> &value) const
{
    // The exact SQLite doubles of a stream read from the log, in its time
    // order, for transforms whose results must match the database. Fails for
    // derived streams, which have no column to read.
    const sensordata::StreamInfo *info = plotDataInfo(stream);
    if (!info || info->derived || info->value_column.empty()) {
        return false;
    }
    time.clear();
    value.clear();
    time.reserve(info->samples);
    value.reserve(info->samples);
    std::string error;
    const bool ok = log_.data->forEachRow(
        info->table,
        {info->value_column},
        [&time, &value](double t, const double *values) {
            time.push_back(t);
            value.push_back(values[0]);
        },
        error);
    if (!ok) {
        qWarning().noquote() << "Could not read" << stream.id << ":" << QString::fromStdString(error);
    }
    return ok;
}

void MainWindow::releasePlotData(const SensorStream &stream)
{
    if (log_.data && stream.dataId != sensordata::kNoStream) {
        log_.data->remove(stream.dataId);
    }
}
