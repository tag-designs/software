/**
 * @file    plot_view.cpp
 * @brief   PlotView: QCustomPlot rendering of a PlotSpec.
 *
 * @details The rebuild, axis and cursor behaviour here is the plot code that
 *          used to live in MainWindow (plotting.cpp and interaction.cpp),
 *          moved without change of behaviour; the display policy it used to
 *          mix in is now buildPlotSpec().
 */
#include "plot_view.h"

#include <QPageLayout>
#include <QPrinter>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{

/// True when @p a and @p b differ at most in title and UTC offset, which can be applied without a rebuild.
bool onlyTitleOrUtcDiffer(const PlotSpec &a, const PlotSpec &b)
{
    if (a.title == b.title && a.utcOffsetHours == b.utcOffsetHours) {
        return false; // Identical specs still rebuild, as every refresh always has.
    }
    if (a.timeDomain != b.timeDomain || a.hasXDataRange != b.hasXDataRange
        || a.xDataRange.lower != b.xDataRange.lower || a.xDataRange.upper != b.xDataRange.upper
        || a.eventMarkerTimes != b.eventMarkerTimes || a.metadataRows != b.metadataRows
        || a.traces.size() != b.traces.size() || a.axes.size() != b.axes.size()) {
        return false;
    }
    for (qsizetype i = 0; i < a.traces.size(); i++) {
        const PlotTraceSpec &x = a.traces[i];
        const PlotTraceSpec &y = b.traces[i];
        if (x.id != y.id || x.name != y.name || x.stream != y.stream || x.color != y.color
            || x.penWidth != y.penWidth || x.axis != y.axis) {
            return false;
        }
    }
    for (qsizetype i = 0; i < a.axes.size(); i++) {
        const PlotAxisSpec &x = a.axes[i];
        const PlotAxisSpec &y = b.axes[i];
        if (x.side != y.side || x.label != y.label || x.color != y.color || x.mode != y.mode
            || x.linkedGroup != y.linkedGroup || x.hasRange != y.hasRange
            || x.lower != y.lower || x.upper != y.upper) {
            return false;
        }
    }
    return true;
}

/// Sets @p axis to @p range padded by 5%.
void setPaddedAxisRange(QCPAxis *axis, const QCPRange &range)
{
    const PlotRange padded = paddedRange(PlotRange{range.lower, range.upper});
    axis->setRange(padded.lower, padded.upper);
}

} // namespace

PlotView::PlotView(QWidget *parent) : QWidget(parent)
{
    plot_ = new QCustomPlot(this);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(plot_);

    // The plot is configured for horizontal pan/zoom because the x-axis is time.
    plot_->setContextMenuPolicy(Qt::CustomContextMenu);
    plot_->setBufferDevicePixelRatio(1.0);
    plot_->setInteraction(QCP::iRangeDrag, true);
    plot_->setInteraction(QCP::iRangeZoom, true);
    plot_->axisRect()->setRangeDrag(Qt::Horizontal);
    plot_->axisRect()->setRangeZoom(Qt::Horizontal);
    plot_->xAxis->setLabel(tr("Hour:Minute (UTC)\nMonth/Day/Year"));
    plot_->xAxis2->setVisible(false);
    elapsed_x_axis_ = plot_->axisRect()->addAxis(QCPAxis::atBottom);
    elapsed_x_axis_->setVisible(false);
    elapsed_x_axis_->setLabel(tr("Elapsed Time (s)"));
    title_ = new QCPTextElement(plot_, QString(), QFont(font().family(), 12, QFont::Bold));
    title_->setVisible(false);
    plot_->plotLayout()->insertRow(0);
    plot_->plotLayout()->addElement(0, 0, title_);

    date_ticker_.reset(new QCPAxisTickerDateTime);
    date_ticker_->setDateTimeFormat("hh:mm\nMM/dd/yy");
    date_ticker_->setTimeZone(QTimeZone::UTC);
    plot_->xAxis->setTicker(date_ticker_);

    // Cursor items exist from startup, but stay hidden until a spec with
    // traces places them on the time axis.
    left_cursor_ = new QCPItemLine(plot_);
    left_cursor_->setVisible(false);
    left_cursor_->setPen(QPen(Qt::black, 1, Qt::DashLine));
    right_cursor_ = new QCPItemLine(plot_);
    right_cursor_->setVisible(false);
    right_cursor_->setPen(QPen(Qt::black, 1, Qt::DashLine));
    metadata_box_ = new QCPItemText(plot_);
    metadata_box_->setVisible(false);
    metadata_box_->position->setType(QCPItemPosition::ptAxisRectRatio);
    metadata_box_->position->setAxisRect(plot_->axisRect());
    metadata_box_->position->setCoords(0.02, 0.02);
    metadata_box_->setPositionAlignment(Qt::AlignTop | Qt::AlignLeft);
    metadata_box_->setTextAlignment(Qt::AlignLeft);
    metadata_box_->setFont(QFont("Courier New", 10));
    metadata_box_->setColor(Qt::black);
    metadata_box_->setBrush(QBrush(Qt::white));
    metadata_box_->setPen(QPen(QColor(120, 120, 120)));
    metadata_box_->setPadding(QMargins(10, 8, 10, 8));
    metadata_box_->setLayer("overlay");

    connect(plot_, &QCustomPlot::customContextMenuRequested, this, [this](const QPoint &pos) {
        emit contextMenuRequested(plot_->mapToGlobal(pos));
    });
    connect(plot_, &QCustomPlot::mousePress, this, &PlotView::mousePress);
    connect(plot_, &QCustomPlot::mouseMove, this, &PlotView::mouseMove);
    connect(plot_, &QCustomPlot::mouseRelease, this, &PlotView::mouseRelease);
    connect(plot_, &QCustomPlot::mouseDoubleClick, this, &PlotView::mouseDoubleClick);
    // Graphs hold only the envelope of the visible window; refill it once
    // per replot, after layout has fixed the plot's width.
    connect(plot_, &QCustomPlot::afterLayout, this, [this]() { refillAfterLayout(); });
    for (QCPAxis *axis : {plot_->xAxis, elapsed_x_axis_}) {
        connect(axis, qOverload<const QCPRange &>(&QCPAxis::rangeChanged), this,
                [this, axis](const QCPRange &range) {
                    if (axis == timeAxis()) {
                        emit xRangeChanged(range.lower, range.upper);
                    }
                });
    }
}

void PlotView::setLog(const sensordata::Log *log)
{
    plot_->clearGraphs();
    graphs_.clear();
    fill_valid_ = false;
    log_ = log;
    has_spec_ = false;
}

void PlotView::setSpec(const PlotSpec &spec)
{
    const bool in_place = has_spec_ && onlyTitleOrUtcDiffer(spec_, spec);
    spec_ = spec;
    has_spec_ = true;
    applyTimeAxis();
    applyTitleAndMetadata();
    if (in_place) {
        plot_->replot();
        return;
    }
    rebuild();
}

QCPAxis *PlotView::timeAxis() const
{
    return spec_.timeDomain == SensorTimeDomain::ElapsedSeconds ? elapsed_x_axis_ : plot_->xAxis;
}

void PlotView::applyTimeAxis()
{
    // Elapsed-time logs use their own axis with plain second ticks; epoch
    // logs use date/time ticks in the chosen UTC offset. Stored times stay
    // UTC epoch seconds; the offset changes only labels.
    if (spec_.timeDomain == SensorTimeDomain::ElapsedSeconds) {
        plot_->xAxis->setVisible(false);
        plot_->xAxis2->setVisible(false);
        elapsed_x_axis_->setVisible(true);
        plot_->axisRect()->setRangeDragAxes(elapsed_x_axis_, nullptr);
        plot_->axisRect()->setRangeZoomAxes(elapsed_x_axis_, nullptr);
        return;
    }

    plot_->xAxis2->setVisible(false);
    elapsed_x_axis_->setVisible(false);
    plot_->xAxis->setVisible(true);
    plot_->axisRect()->setRangeDragAxes(plot_->xAxis, nullptr);
    plot_->axisRect()->setRangeZoomAxes(plot_->xAxis, nullptr);
    const int offset = spec_.utcOffsetHours;
    date_ticker_->setTimeZone(QTimeZone(3600 * offset));
    date_ticker_->setTickOrigin(-3600 * offset);
    if (offset == 0) {
        plot_->xAxis->setLabel(tr("Hour:Minute (UTC)\nMonth/Day/Year"));
    } else {
        plot_->xAxis->setLabel(tr("Hour:Minute (UTC%1%2)\nMonth/Day/Year")
                                   .arg(offset > 0 ? "+" : "")
                                   .arg(offset));
    }
}

void PlotView::applyTitleAndMetadata()
{
    title_->setText(spec_.title);
    title_->setVisible(!spec_.title.isEmpty());
    metadata_box_->setText(spec_.metadataRows.join('\n'));
    metadata_box_->setVisible(!spec_.metadataRows.isEmpty());
}

void PlotView::rebuild()
{
    // Rebuilding graphs and axes from scratch is simpler and less error-prone
    // than keeping QCustomPlot objects in sync with a changing spec.
    const bool preserve_x_range = plot_->graphCount() > 0;
    QCPAxis *x_axis = timeAxis();
    const QCPRange old_x_range = x_axis->range();

    plot_->clearGraphs();
    graphs_.clear();
    fill_valid_ = false;
    clearEventMarkers();
    clearDynamicAxes();

    int left_axis_count = 0;
    int right_axis_count = 0;
    for (const PlotTraceSpec &trace : spec_.traces) {
        const PlotAxisSpec &axis_spec = spec_.axes[trace.axis];
        const int side_index = axis_spec.side == SensorAxisSide::Right
            ? right_axis_count++
            : left_axis_count++;
        QCPAxis *axis = addValueAxis(axis_spec.side, side_index, axis_spec);
        QCPGraph *graph = plot_->addGraph(x_axis, axis);
        graph->setName(trace.name);
        graph->setPen(QPen(trace.color, trace.penWidth));
        if (trace.stream != sensordata::kNoStream) {
            graphs_.append(qMakePair(graph, trace.stream));
        }

        switch (axis_spec.mode) {
        case PlotAxisRangeMode::Fixed:
        case PlotAxisRangeMode::Linked:
            axis->setRange(axis_spec.lower, axis_spec.upper);
            break;
        case PlotAxisRangeMode::Auto:
            // QCPGraph::rescaleValueAxis(true): enlarge the data range by the
            // axis's current range, which for the reused first left and right
            // axes carries over from earlier rebuilds.
            if (axis_spec.hasRange) {
                QCPRange range(axis_spec.lower, axis_spec.upper);
                range.expand(axis->range());
                if (!QCPRange::validRange(range)) {
                    const double center = (range.lower + range.upper) * 0.5;
                    range.lower = center - axis->range().size() / 2.0;
                    range.upper = center + axis->range().size() / 2.0;
                }
                axis->setRange(range);
            }
            setPaddedAxisRange(axis, axis->range());
            break;
        }
    }

    if (!spec_.traces.isEmpty()) {
        if (preserve_x_range) {
            x_axis->setRange(old_x_range);
        } else {
            resetXRangeToData();
        }
        addEventMarkers();
        resetCursorsToView();
    } else {
        left_cursor_->setVisible(false);
        right_cursor_->setVisible(false);
    }
    plot_->replot();
}

void PlotView::resetXRangeToData()
{
    // QCPAxis::rescale(true) over the traces' key ranges, then 5% each side.
    QCPAxis *x_axis = timeAxis();
    if (spec_.hasXDataRange) {
        QCPRange range(spec_.xDataRange.lower, spec_.xDataRange.upper);
        if (!QCPRange::validRange(range)) {
            const double center = (range.lower + range.upper) * 0.5;
            range.lower = center - x_axis->range().size() / 2.0;
            range.upper = center + x_axis->range().size() / 2.0;
        }
        x_axis->setRange(range);
    }
    x_axis->scaleRange(1.05, x_axis->range().center());
}

void PlotView::resetZoom()
{
    if (plot_->graphCount() == 0) {
        return;
    }
    resetXRangeToData();
    resetCursorsToView();
    plot_->replot();
}

QCPAxis *PlotView::addValueAxis(SensorAxisSide side, int side_index, const PlotAxisSpec &spec)
{
    // One value axis per visible stream so unrelated units do not compete for
    // a single y scale. The first left/right axes reuse QCustomPlot's
    // built-in ones; additional axes are tracked in dynamic_axes_ for removal.
    QCPAxis *axis = nullptr;
    if (side == SensorAxisSide::Right) {
        axis = side_index == 0 ? plot_->yAxis2 : plot_->axisRect()->addAxis(QCPAxis::atRight);
    } else {
        axis = side_index == 0 ? plot_->yAxis : plot_->axisRect()->addAxis(QCPAxis::atLeft);
    }
    if (side_index > 0) {
        dynamic_axes_.append(axis);
    }

    axis->setVisible(true);
    axis->setLabel(spec.label);
    axis->setLabelColor(spec.color);
    axis->setTickLabelColor(spec.color);
    axis->setBasePen(QPen(spec.color));
    axis->setTickPen(QPen(spec.color));
    axis->setSubTickPen(QPen(spec.color));
    return axis;
}

void PlotView::clearDynamicAxes()
{
    // The first two axes are built into QCustomPlot, so hide rather than
    // remove them when no trace is currently assigned to that side.
    QCPAxisRect *axis_rect = plot_->axisRect();
    for (QCPAxis *axis : dynamic_axes_) {
        axis_rect->removeAxis(axis);
    }
    dynamic_axes_.clear();
    plot_->yAxis->setVisible(false);
    plot_->yAxis2->setVisible(false);
}

void PlotView::clearEventMarkers()
{
    for (QCPItemStraightLine *marker : event_marker_items_) {
        plot_->removeItem(marker);
    }
    event_marker_items_.clear();
}

void PlotView::addEventMarkers()
{
    QCPAxis *x_axis = timeAxis();
    QPen marker_pen(QColor(190, 65, 55), 1, Qt::DashLine);
    for (double time : spec_.eventMarkerTimes) {
        QCPItemStraightLine *line = new QCPItemStraightLine(plot_);
        line->setClipToAxisRect(true);
        line->setClipAxisRect(plot_->axisRect());
        line->setLayer("overlay");
        line->setPen(marker_pen);
        line->setSelectable(false);
        line->point1->setAxes(x_axis, plot_->yAxis);
        line->point2->setAxes(x_axis, plot_->yAxis);
        line->point1->setCoords(time, 0.0);
        line->point2->setCoords(time, 1.0);
        event_marker_items_.append(line);
    }
}

void PlotView::resetCursorsToView()
{
    // Keep the two zoom cursors bracketing whatever time window is visible.
    QCPAxis *x_axis = timeAxis();
    left_cursor_->start->setAxes(x_axis, plot_->yAxis);
    left_cursor_->end->setAxes(x_axis, plot_->yAxis);
    right_cursor_->start->setAxes(x_axis, plot_->yAxis);
    right_cursor_->end->setAxes(x_axis, plot_->yAxis);

    left_cursor_->setVisible(cursors_enabled_);
    right_cursor_->setVisible(cursors_enabled_);
    left_cursor_->start->setCoords(x_axis->range().lower, QCPRange::minRange);
    left_cursor_->end->setCoords(x_axis->range().lower, QCPRange::maxRange);
    right_cursor_->start->setCoords(x_axis->range().upper, QCPRange::minRange);
    right_cursor_->end->setCoords(x_axis->range().upper, QCPRange::maxRange);
}

void PlotView::setCursorsVisible(bool visible)
{
    cursors_enabled_ = visible;
    if (visible) {
        resetCursorsToView();
    } else {
        left_cursor_->setVisible(false);
        right_cursor_->setVisible(false);
    }
    plot_->replot();
}

void PlotView::zoomToCursors()
{
    // Apply the cursor interval to the time axis without changing y ranges.
    if (!left_cursor_->visible()) {
        return;
    }
    const double left = left_cursor_->start->coords().x();
    const double right = right_cursor_->start->coords().x();
    if (left < right) {
        timeAxis()->setRange(left, right);
        plot_->replot();
    }
}

QPair<double, double> PlotView::xRange() const
{
    const QCPRange range = timeAxis()->range();
    return qMakePair(range.lower, range.upper);
}

void PlotView::mouseDoubleClick(QMouseEvent *event)
{
    // Mouse convention inherited from compviz: double-click moves the left
    // cursor, shift-double-click moves the right cursor.
    if (!cursors_enabled_) {
        cursors_enabled_ = true;
        resetCursorsToView();
        emit cursorsVisibleChanged(true);
    }
    if (!left_cursor_->visible()) {
        resetCursorsToView();
    }

    const double x = timeAxis()->pixelToCoord(event->pos().x());
    if ((event->button() & Qt::LeftButton)
        && !(event->modifiers() & Qt::ShiftModifier)
        && x <= right_cursor_->start->coords().x()) {
        left_cursor_->start->setCoords(x, QCPRange::minRange);
        left_cursor_->end->setCoords(x, QCPRange::maxRange);
        qDebug() << "move left cursor:" << x;
    } else if ((event->button() & Qt::LeftButton)
               && (event->modifiers() & Qt::ShiftModifier)
               && x >= left_cursor_->start->coords().x()) {
        right_cursor_->start->setCoords(x, QCPRange::minRange);
        right_cursor_->end->setCoords(x, QCPRange::maxRange);
        qDebug() << "move right cursor:" << x;
    }

    plot_->replot();
    emit cursorsChanged(left_cursor_->start->coords().x(), right_cursor_->start->coords().x());
}

void PlotView::mousePress(QMouseEvent *event)
{
    // The metadata box is a movable annotation, not part of the data. While it
    // is being dragged, range dragging is off so the plot does not pan
    // underneath the user's hand.
    if (!metadata_box_->visible() || event->button() != Qt::LeftButton) {
        return;
    }
    if (plot_->itemAt(event->pos()) != metadata_box_) {
        return;
    }
    metadata_box_dragging_ = true;
    metadata_box_drag_start_pixel_ = event->pos();
    metadata_box_drag_start_coords_ = metadata_box_->position->coords();
    plot_->setInteraction(QCP::iRangeDrag, false);
}

void PlotView::mouseMove(QMouseEvent *event)
{
    if (metadata_box_dragging_) {
        // QCPItemText is positioned in axis-rect-ratio coordinates. Updating
        // by pixel delta preserves the chosen relative placement across later
        // zooms and pans.
        const QRect rect = plot_->axisRect()->rect();
        if (rect.width() <= 0 || rect.height() <= 0) {
            return;
        }
        const QPoint delta = event->pos() - metadata_box_drag_start_pixel_;
        const double x =
            metadata_box_drag_start_coords_.x() + static_cast<double>(delta.x()) / rect.width();
        const double y =
            metadata_box_drag_start_coords_.y() + static_cast<double>(delta.y()) / rect.height();
        metadata_box_->position->setCoords(std::clamp(x, 0.0, 0.98), std::clamp(y, 0.0, 0.98));
        plot_->replot();
        return;
    }
    if (!plot_->axisRect()->rect().contains(event->pos())) {
        return;
    }
    emit hoverTime(timeAxis()->pixelToCoord(event->pos().x()));
}

void PlotView::mouseRelease(QMouseEvent *event)
{
    Q_UNUSED(event);
    if (!metadata_box_dragging_) {
        return;
    }
    metadata_box_dragging_ = false;
    plot_->setInteraction(QCP::iRangeDrag, true);
}

int PlotView::plotColumns(double device_pixels_per_pixel) const
{
    // One envelope column per device pixel across the data area.
    const int width = timeAxis()->axisRect()->width();
    return std::max(1, static_cast<int>(std::lround(width * device_pixels_per_pixel)));
}

void PlotView::fillGraphs(double lower, double upper, int columns)
{
    // Replace every graph's points with the M4 envelope of [lower, upper]:
    // first, last, minimum and maximum sample of each column, plus one sample
    // beyond each edge. Drawn as a line, that is the same picture as every
    // sample, at a few thousand points per graph.
    fill_lower_ = lower;
    fill_upper_ = upper;
    fill_columns_ = columns;
    fill_valid_ = true;
    if (!log_) {
        return;
    }
    for (const auto &entry : graphs_) {
        const sensordata::Envelope envelope = log_->view(entry.second, lower, upper, columns);
        QVector<QCPGraphData> points(static_cast<qsizetype>(envelope.index.size()));
        for (qsizetype i = 0; i < points.size(); i++) {
            points[i] = QCPGraphData(envelope.time[static_cast<size_t>(i)],
                                     envelope.value[static_cast<size_t>(i)]);
        }
        entry.first->data()->set(points, true);
    }
}

void PlotView::refillAfterLayout()
{
    // Connected to QCustomPlot::afterLayout, which every replot (and every
    // toPainter() print) emits after the axis rect has its final size and
    // before anything is drawn.
    if (graphs_.isEmpty()) {
        return;
    }
    const QCPRange range = timeAxis()->range();
    const int columns = plotColumns(
        print_pixel_scale_ > 0.0 ? print_pixel_scale_ : plot_->devicePixelRatioF());
    if (fill_valid_ && range.lower == fill_lower_ && range.upper == fill_upper_
        && columns == fill_columns_) {
        return;
    }
    fillGraphs(range.lower, range.upper, columns);
}

void PlotView::render(QPrinter *printer, int device_dpi)
{
    // Paint in the screen's coordinate system. QPainter sizes text from the
    // paint device's resolution, so at the printer's own (1200 dpi for a PDF)
    // text would come out many times too large once the page scale below is
    // applied. The output is still vector; the resolution only sets the
    // coordinate system, and must be set before painting begins.
    if (device_dpi <= 0) {
        device_dpi = printer->resolution();
    }
    printer->setResolution(plot_->logicalDpiX());
    QCPPainter painter(printer);
    const auto page_layout = printer->pageLayout();
    const auto page_rect = page_layout.paintRectPixels(printer->resolution());
    const double xscale = page_rect.width() / double(plot_->width());
    const double yscale = page_rect.height() / double(plot_->height());
    const double scale = qMin(xscale, yscale);

    // Preserve the on-screen aspect ratio and let QCustomPlot render vector
    // output where the platform print backend supports it. toPainter() draws
    // the plot itself; QWidget::render() would copy the screen's raster
    // buffers.
    painter.scale(scale, scale);
    painter.setMode(QCPPainter::pmVectorized);
    painter.setMode(QCPPainter::pmNoCaching);
    painter.setMode(QCPPainter::pmNonCosmetic);

    // toPainter() runs a layout pass, whose refill sizes the envelope for the
    // printer's device resolution while print_pixel_scale_ is set.
    // QCustomPlot's adaptive sampling works in these screen-sized coordinates
    // and would thin that envelope back down, so it is off while printing.
    print_pixel_scale_ = scale * device_dpi / double(printer->resolution());
    for (const auto &entry : graphs_) {
        entry.first->setAdaptiveSampling(false);
    }
    plot_->toPainter(&painter, plot_->width(), plot_->height());
    for (const auto &entry : graphs_) {
        entry.first->setAdaptiveSampling(true);
    }
    print_pixel_scale_ = 0.0;
    plot_->replot(QCustomPlot::rpQueuedReplot);
}
