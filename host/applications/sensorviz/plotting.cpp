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

void MainWindow::registerPlotData(SensorStream &stream)
{
    // Give a stream built by a transform its own plot data in log_.data.
    // Transforms usually reuse an input stream's time vector (magnitudes,
    // altitude, activity filter); when this stream's time is that same
    // shared QVector, the plot data shares the input's time axis too.
    stream.dataId = sensordata::kNoStream;
    if (!log_.data || stream.time.isEmpty() || stream.time.size() != stream.value.size()) {
        return;
    }

    sensordata::StreamInfo info;
    info.id = stream.id.toStdString();
    info.label = stream.label.toStdString();
    info.units = stream.units.toStdString();
    info.kind = "derived";
    info.domain = stream.timeDomain == SensorTimeDomain::ElapsedSeconds
        ? sensordata::TimeDomain::ElapsedSeconds
        : sensordata::TimeDomain::EpochSeconds;

    for (const SensorStream &source : streams_) {
        if (source.dataId != sensordata::kNoStream
            && source.time.constData() == stream.time.constData()) {
            const std::optional<sensordata::StreamId> id = log_.data->addDerived(
                info, source.dataId, stream.value.constData(),
                static_cast<std::size_t>(stream.value.size()));
            if (id) {
                stream.dataId = *id;
                return;
            }
        }
    }

    const std::optional<sensordata::StreamId> id = log_.data->addDerived(
        info,
        std::vector<double>(stream.time.cbegin(), stream.time.cend()),
        std::vector<double>(stream.value.cbegin(), stream.value.cend()));
    if (id) {
        stream.dataId = *id;
    }
}

void MainWindow::releasePlotData(const SensorStream &stream)
{
    if (log_.data && stream.dataId != sensordata::kNoStream) {
        log_.data->remove(stream.dataId);
    }
}
