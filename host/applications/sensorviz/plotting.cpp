#include "mainwindow.h"

#include <QDateTime>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace
{

QString linkedAxisGroupId(const QString &stream_id)
{
    // Related physical axes should be compared on the same value scale. Keep
    // this narrowly scoped to true x/y/z families so unrelated streams with the
    // same units can still auto-scale independently.
    if (stream_id == "imu_ax" || stream_id == "imu_ay" || stream_id == "imu_az") {
        return QStringLiteral("imu_accel");
    }
    if (stream_id == "imu_gx" || stream_id == "imu_gy" || stream_id == "imu_gz") {
        return QStringLiteral("imu_gyro");
    }
    if (stream_id == "imu_mx" || stream_id == "imu_my" || stream_id == "imu_mz") {
        return QStringLiteral("imu_mag");
    }
    return {};
}

bool valueRangeForStream(const SensorStream &stream, QCPRange &range)
{
    bool have_value = false;
    double lower = 0.0;
    double upper = 0.0;
    for (double value : stream.value) {
        if (!std::isfinite(value)) {
            continue;
        }
        if (!have_value) {
            lower = value;
            upper = value;
            have_value = true;
        } else {
            lower = std::min(lower, value);
            upper = std::max(upper, value);
        }
    }
    if (!have_value) {
        return false;
    }

    range = QCPRange(lower, upper);
    return true;
}

void extendRange(QCPRange &range, const QCPRange &addition)
{
    range.lower = std::min(range.lower, addition.lower);
    range.upper = std::max(range.upper, addition.upper);
}

void appendMetadataRow(QStringList &rows, const QString &label, const QString &value)
{
    rows.append(QString("%1 %2").arg(label + ":", -8).arg(value));
}

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
    QCPAxis *x_axis = timeAxis();
    const QCPRange old_x_range = x_axis->range();

    updateGraphTitle();
    plot_->clearGraphs();
    clearDynamicAxes();

    const QVector<SensorStream> visible = visibleStreams();
    QMap<QString, int> linked_group_counts;
    QMap<QString, QCPRange> linked_group_ranges;
    for (const SensorStream &stream : visible) {
        const QString group_id = linkedAxisGroupId(stream.id);
        if (group_id.isEmpty()
            || custom_axis_ranges_.contains(stream.id)
            || stream.axisRange.enabled) {
            continue;
        }

        QCPRange stream_range;
        if (!valueRangeForStream(stream, stream_range)) {
            continue;
        }
        linked_group_counts[group_id]++;
        if (linked_group_ranges.contains(group_id)) {
            extendRange(linked_group_ranges[group_id], stream_range);
        } else {
            linked_group_ranges[group_id] = stream_range;
        }
    }

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
        QCPGraph *graph = plot_->addGraph(x_axis, axis);
        graph->setName(stream.label);
        graph->setPen(QPen(effectiveStreamColor(stream), stream.derived ? 2 : 1));
        graph->setData(stream.time, stream.value, true);
        if (custom_axis_ranges_.contains(stream.id)) {
            axis->setRange(custom_axis_ranges_[stream.id]);
        } else if (stream.axisRange.enabled) {
            axis->setRange(stream.axisRange.lower, stream.axisRange.upper);
        } else if (linked_group_counts.value(linkedAxisGroupId(stream.id)) > 1) {
            setPaddedAxisRange(axis, linked_group_ranges[linkedAxisGroupId(stream.id)]);
        } else {
            // Each stream owns its value axis because sensors use different
            // units and ranges.
            graph->rescaleValueAxis(true);
            setPaddedAxisRange(axis, axis->range());
        }
    }

    if (metadata_box_) {
        QStringList metadata_rows;
        if (log_.timeDomain == SensorTimeDomain::ElapsedSeconds && log_.hasCollectionStart) {
            const qint64 rounded_epoch_ms =
                ((log_.collectionStartEpochMs + 500) / 1000) * 1000;
            const QDateTime start_time =
                QDateTime::fromMSecsSinceEpoch(rounded_epoch_ms, QTimeZone::UTC);
            appendMetadataRow(
                metadata_rows,
                tr("Start"),
                start_time.toString("MM/dd/yy hh:mm:ss") + QStringLiteral(" UTC"));
        }
        if (heading_visible) {
            appendMetadataRow(
                metadata_rows,
                tr("Decl"),
                tr("%1 deg").arg(declination_degrees_, 0, 'f', 2));
        }
        if (altitude_visible) {
            appendMetadataRow(
                metadata_rows,
                tr("MSL"),
                tr("%1 mbar").arg(sea_level_pressure_, 0, 'f', 2));
        }
        metadata_box_->setText(metadata_rows.join('\n'));
        metadata_box_->setVisible(!metadata_rows.isEmpty());
    }

    if (!visible.isEmpty()) {
        if (preserve_x_range) {
            x_axis->setRange(old_x_range);
        } else {
            x_axis->rescale(true);
            x_axis->scaleRange(1.05, x_axis->range().center());
        }
        resetCursorsToView();
    } else {
        left_cursor_->setVisible(false);
        right_cursor_->setVisible(false);
    }
    plot_->replot();
}

QCPAxis *MainWindow::timeAxis() const
{
    return log_.timeDomain == SensorTimeDomain::ElapsedSeconds && elapsed_x_axis_
        ? elapsed_x_axis_
        : plot_->xAxis;
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
