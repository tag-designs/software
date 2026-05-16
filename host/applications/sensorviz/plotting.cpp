#include "mainwindow.h"

#include <algorithm>
#include <cmath>

namespace
{

void setPaddedAxisRange(QCPAxis *axis, const QCPRange &range)
{
    // QCustomPlot's rescale can put max/min exactly on the frame. Add a small
    // margin so peaks remain visible and flat signals still get a useful span.
    double lower = range.lower;
    double upper = range.upper;
    double span = upper - lower;
    if (span <= 0.0) {
        span = std::max(1.0, std::abs(range.center()) * 0.1);
        lower = range.center() - span * 0.5;
        upper = range.center() + span * 0.5;
    }

    const double margin = span * 0.05;
    axis->setRange(lower - margin, upper + margin);
}

} // namespace

// QCustomPlot graphs and dynamic axes are rebuilt on every refresh. That is
// simpler than trying to mutate old graph state when stream actions change, and
// it keeps derived-stream removal straightforward.

void MainWindow::clearDynamicAxes()
{
    // Remove axes created for previous stream combinations before rebuilding
    // the plot. Built-in yAxis/yAxis2 are hidden because QCustomPlot owns them.
    QCPAxisRect *axis_rect = plot_->axisRect();
    for (QCPAxis *axis : dynamic_axes_) {
        axis_rect->removeAxis(axis);
    }
    dynamic_axes_.clear();
    // The first two axes are built into QCustomPlot, so hide rather than remove
    // them when no stream is currently assigned to that side.
    plot_->yAxis->setVisible(false);
    plot_->yAxis2->setVisible(false);
}

QCPAxis *MainWindow::axisForStream(
    SensorAxisSide side,
    int side_index,
    const SensorStream &stream)
{
    // Allocate one value axis per visible stream so unrelated units do not
    // compete for a single y scale. The first left/right axes reuse QCustomPlot
    // defaults; additional axes are tracked in dynamic_axes_ for removal.
    const QColor color = effectiveStreamColor(stream);
    QCPAxis *axis = nullptr;
    if (side == SensorAxisSide::Right) {
        axis = side_index == 0
            ? plot_->yAxis2
            : plot_->axisRect()->addAxis(QCPAxis::atRight);
    } else {
        axis = side_index == 0
            ? plot_->yAxis
            : plot_->axisRect()->addAxis(QCPAxis::atLeft);
    }

    if ((side == SensorAxisSide::Right && side_index > 0)
        || (side == SensorAxisSide::Left && side_index > 0)) {
        dynamic_axes_.append(axis);
    }

    axis->setVisible(true);
    axis->setLabel(stream.label + unitsSuffix(stream));
    axis->setLabelColor(color);
    axis->setTickLabelColor(color);
    axis->setBasePen(QPen(color));
    axis->setTickPen(QPen(color));
    axis->setSubTickPen(QPen(color));
    return axis;
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
    // Rebuilding graphs and axes from scratch is simpler and less error-prone
    // than keeping QCustomPlot objects in sync with changing stream/action
    // state. The data vectors live in streams_; QCustomPlot is just the current
    // rendering of that state.
    const bool preserve_x_range = !reset_x_range && plot_->graphCount() > 0;
    const QCPRange old_x_range = plot_->xAxis->range();

    updateGraphTitle();
    plot_->clearGraphs();
    clearDynamicAxes();

    const QVector<SensorStream> visible = visibleStreams();
    bool heading_visible = false;
    bool altitude_visible = false;
    int left_axis_count = 0;
    int right_axis_count = 0;
    for (const SensorStream &stream : visible) {
        heading_visible = heading_visible || stream.id == "compass_heading";
        altitude_visible = altitude_visible || stream.id == "altitude";
        const SensorAxisSide side = effectiveAxisSide(stream);
        const int side_index = side == SensorAxisSide::Right
            ? right_axis_count++
            : left_axis_count++;
        QCPAxis *axis = axisForStream(side, side_index, stream);
        QCPGraph *graph = plot_->addGraph(plot_->xAxis, axis);
        graph->setName(stream.label);
        graph->setPen(QPen(effectiveStreamColor(stream), stream.derived ? 2 : 1));
        graph->setData(stream.time, stream.value, true);
        if (custom_axis_ranges_.contains(stream.id)) {
            axis->setRange(custom_axis_ranges_[stream.id]);
        } else if (stream.axisRange.enabled) {
            axis->setRange(stream.axisRange.lower, stream.axisRange.upper);
        } else {
            // Each stream owns its value axis because sensors use different
            // units and ranges.
            graph->rescaleValueAxis(true);
            setPaddedAxisRange(axis, axis->range());
        }
    }

    int status_label_row = 0;
    if (declination_label_) {
        declination_label_->setVisible(heading_visible);
        declination_label_->setText(
            tr("Declination: %1 deg").arg(declination_degrees_, 0, 'f', 2));
        declination_label_->position->setCoords(0.0, 1.08 + 0.06 * status_label_row);
        if (heading_visible) {
            status_label_row++;
        }
    }
    if (sea_level_pressure_label_) {
        sea_level_pressure_label_->setVisible(altitude_visible);
        sea_level_pressure_label_->setText(
            tr("Sea-level pressure: %1 mbar").arg(sea_level_pressure_, 0, 'f', 2));
        sea_level_pressure_label_->position->setCoords(0.0, 1.08 + 0.06 * status_label_row);
    }

    if (!visible.isEmpty()) {
        if (preserve_x_range) {
            plot_->xAxis->setRange(old_x_range);
        } else {
            plot_->xAxis->rescale(true);
            plot_->xAxis->scaleRange(1.05, plot_->xAxis->range().center());
        }
        resetCursorsToView();
    } else {
        left_cursor_->setVisible(false);
        right_cursor_->setVisible(false);
    }
    plot_->replot();
}

void MainWindow::resetZoom()
{
    // User-visible command: clear all manual y ranges and show the full loaded
    // time span again.
    if (plot_->graphCount() == 0) {
        return;
    }

    // Reset Zoom means "return to the metadata/data defaults", not merely
    // "show the full time window". Clear both manual ranges and linked ranges
    // such as the altitude range derived from pressure.
    custom_axis_ranges_.clear();
    explicit_axis_ranges_.clear();
    refreshPlotFullRange();
}
