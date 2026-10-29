#include "mainwindow.h"

// MainWindow's side of the plot: the spec is built in the Qt-free core
// (session_.plotSpec(), policy in core/plot_spec.cpp) and handed to PlotView,
// which draws it.

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
    plot_view_->setSpec(session_.plotSpec());
    if (reset_x_range) {
        plot_view_->resetZoom();
    }
}

void MainWindow::resetZoom()
{
    // User-visible command: clear all manual y ranges and show the full loaded
    // time span again.
    if (plot_view_->spec().traces.empty()) {
        return;
    }

    // Reset Zoom means "return to the metadata/data defaults", not merely
    // "show the full time window". Clear both manual ranges and linked ranges
    // such as the altitude range derived from pressure.
    session_.resetZoom();
    refreshPlotFullRange();
}

const sensordata::StreamInfo *MainWindow::plotDataInfo(const SensorStream &stream) const
{
    return log_.data ? log_.data->info(stream.dataId) : nullptr;
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
        qWarning().noquote() << "Could not read" << QString::fromStdString(stream.id) << ":"
                             << QString::fromStdString(error);
    }
    return ok;
}

void MainWindow::releasePlotData(const SensorStream &stream)
{
    if (log_.data && stream.dataId != sensordata::kNoStream) {
        log_.data->remove(stream.dataId);
    }
}
