#include "mainwindow.h"

#include <QAction>
#include <QDateTime>
#include <QInputDialog>
#include <QMenu>
#include <QPageLayout>
#include <QPageSize>
#include <QPrintPreviewDialog>
#include <QTimeZone>
#include <QToolTip>

#include <algorithm>
#include <cmath>

// User interaction that depends on an existing plot lives here: cursors,
// context menus, printing, UTC offset selection, and mouse-position readout.

void MainWindow::resetCursorsToView()
{
    // Keep the two zoom cursors bracketing whatever time window is currently
    // visible. Called after plot rebuilds and full-range resets.
    if (!left_cursor_ || !right_cursor_) {
        return;
    }

    QCPAxis *x_axis = timeAxis();
    left_cursor_->start->setAxes(x_axis, plot_->yAxis);
    left_cursor_->end->setAxes(x_axis, plot_->yAxis);
    right_cursor_->start->setAxes(x_axis, plot_->yAxis);
    right_cursor_->end->setAxes(x_axis, plot_->yAxis);

    const bool show_cursors = !show_cursors_action_ || show_cursors_action_->isChecked();
    left_cursor_->setVisible(show_cursors);
    right_cursor_->setVisible(show_cursors);
    left_cursor_->start->setCoords(x_axis->range().lower, QCPRange::minRange);
    left_cursor_->end->setCoords(x_axis->range().lower, QCPRange::maxRange);
    right_cursor_->start->setCoords(x_axis->range().upper, QCPRange::minRange);
    right_cursor_->end->setCoords(x_axis->range().upper, QCPRange::maxRange);
}

void MainWindow::plotDoubleClick(QMouseEvent *event)
{
    // Mouse convention inherited from compviz: double-click moves the left
    // cursor, shift-double-click moves the right cursor.
    if (!left_cursor_ || !right_cursor_) {
        return;
    }
    if (show_cursors_action_ && !show_cursors_action_->isChecked()) {
        show_cursors_action_->setChecked(true);
    }
    if (!left_cursor_->visible()) {
        resetCursorsToView();
    }

    QCPAxis *x_axis = timeAxis();
    const double x = x_axis->pixelToCoord(event->pos().x());
    // Double-click sets the left cursor; shift-double-click sets the right.
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
}

void MainWindow::zoomToCursors()
{
    // Apply the cursor interval to the x-axis without changing y ranges.
    if (!left_cursor_ || !right_cursor_ || !left_cursor_->visible()) {
        return;
    }

    const double left = left_cursor_->start->coords().x();
    const double right = right_cursor_->start->coords().x();
    if (left < right) {
        timeAxis()->setRange(left, right);
        plot_->replot();
    }
}

void MainWindow::setCursorsVisible(bool visible)
{
    if (!left_cursor_ || !right_cursor_) {
        return;
    }

    if (visible) {
        resetCursorsToView();
        plot_->replot();
    } else {
        left_cursor_->setVisible(false);
        right_cursor_->setVisible(false);
        plot_->replot();
    }
}

void MainWindow::beginMetadataBoxDrag(QMouseEvent *event)
{
    if (!metadata_box_ || !metadata_box_->visible() || event->button() != Qt::LeftButton) {
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

void MainWindow::dragMetadataBox(QMouseEvent *event)
{
    if (!metadata_box_dragging_ || !metadata_box_) {
        return;
    }

    const QRect rect = plot_->axisRect()->rect();
    if (rect.width() <= 0 || rect.height() <= 0) {
        return;
    }

    const QPoint delta = event->pos() - metadata_box_drag_start_pixel_;
    const double x =
        metadata_box_drag_start_coords_.x() + static_cast<double>(delta.x()) / rect.width();
    const double y =
        metadata_box_drag_start_coords_.y() + static_cast<double>(delta.y()) / rect.height();
    metadata_box_->position->setCoords(
        std::clamp(x, 0.0, 0.98),
        std::clamp(y, 0.0, 0.98));
    plot_->replot();
}

void MainWindow::endMetadataBoxDrag(QMouseEvent *event)
{
    Q_UNUSED(event);
    if (!metadata_box_dragging_) {
        return;
    }

    metadata_box_dragging_ = false;
    plot_->setInteraction(QCP::iRangeDrag, true);
}

void MainWindow::printPlot()
{
    // Open a print preview for the current plot frame. The actual rendering is
    // delegated to renderPlot() because Qt calls it from the preview dialog.
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::NativeFormat);
    printer.setPageOrientation(QPageLayout::Landscape);
    printer.setPageSize(QPageSize(QPageSize::Letter));

    QPrintPreviewDialog preview(&printer, this);
    connect(&preview, &QPrintPreviewDialog::paintRequested, this, &MainWindow::renderPlot);
    preview.exec();
}

void MainWindow::renderPlot(QPrinter *printer)
{
    // Render QCustomPlot into the print device using vector-friendly painter
    // settings. Page scaling preserves the on-screen aspect ratio.
    QCPPainter painter(printer);
    const auto page_layout = printer->pageLayout();
    const auto page_rect = page_layout.paintRectPixels(printer->resolution());
    const double xscale = page_rect.width() / double(plot_->width());
    const double yscale = page_rect.height() / double(plot_->height());
    // Preserve the on-screen aspect ratio and let QCustomPlot render vector
    // output where the platform print backend supports it.
    painter.scale(qMin(xscale, yscale), qMin(xscale, yscale));
    painter.setMode(QCPPainter::pmVectorized);
    painter.setMode(QCPPainter::pmNoCaching);
    painter.setMode(QCPPainter::pmNonCosmetic);
    plot_->render(&painter);
}

void MainWindow::setUtcOffset()
{
    if (log_.timeDomain == SensorTimeDomain::ElapsedSeconds) {
        return;
    }

    // Change only tick labels and x-axis title. Stored stream times remain Unix
    // epoch seconds in UTC.
    bool ok = false;
    const int hours = QInputDialog::getInt(
        this,
        tr("UTC Offset"),
        tr("Offset from UTC"),
        utc_offset_,
        -12,
        12,
        1,
        &ok);
    if (!ok) {
        return;
    }

    utc_offset_ = hours;
    applyUtcAxisLabel();
    plot_->replot();
}

void MainWindow::applyUtcAxisLabel()
{
    date_ticker_->setTimeZone(QTimeZone(3600 * utc_offset_));
    date_ticker_->setTickOrigin(-3600 * utc_offset_);
    if (utc_offset_ == 0) {
        plot_->xAxis->setLabel(tr("Hour:Minute (UTC)\nMonth/Day/Year"));
    } else {
        plot_->xAxis->setLabel(tr("Hour:Minute (UTC%1%2)\nMonth/Day/Year")
                                   .arg(utc_offset_ > 0 ? "+" : "")
                                   .arg(utc_offset_));
    }
}

QString MainWindow::formatPlotTime(double time_seconds) const
{
    if (log_.timeDomain == SensorTimeDomain::ElapsedSeconds) {
        const bool negative = time_seconds < 0.0;
        double remaining = std::abs(time_seconds);
        const int hours = static_cast<int>(remaining / 3600.0);
        remaining -= hours * 3600.0;
        const int minutes = static_cast<int>(remaining / 60.0);
        remaining -= minutes * 60.0;
        const double seconds = remaining;

        QString prefix = negative ? QStringLiteral("-") : QString();
        if (hours > 0) {
            return QString("%1Elapsed %2:%3:%4 s")
                .arg(prefix)
                .arg(hours)
                .arg(minutes, 2, 10, QLatin1Char('0'))
                .arg(seconds, 6, 'f', 3, QLatin1Char('0'));
        }
        return QString("%1Elapsed %2:%3 s")
            .arg(prefix)
            .arg(minutes)
            .arg(seconds, 6, 'f', 3, QLatin1Char('0'));
    }

    const QDateTime dt =
        QDateTime::fromSecsSinceEpoch(qint64(time_seconds), QTimeZone(3600 * utc_offset_));
    return dt.toString("MM/dd hh:mm:ss");
}

void MainWindow::updateTimeAxisForLog()
{
    if (log_.timeDomain == SensorTimeDomain::ElapsedSeconds) {
        plot_->xAxis->setVisible(false);
        plot_->xAxis2->setVisible(false);
        if (elapsed_x_axis_) {
            elapsed_x_axis_->setVisible(true);
            plot_->axisRect()->setRangeDragAxes(elapsed_x_axis_, nullptr);
            plot_->axisRect()->setRangeZoomAxes(elapsed_x_axis_, nullptr);
        }
        utc_offset_action_->setEnabled(false);
        return;
    }

    plot_->xAxis2->setVisible(false);
    if (elapsed_x_axis_) {
        elapsed_x_axis_->setVisible(false);
    }
    plot_->xAxis->setVisible(true);
    plot_->axisRect()->setRangeDragAxes(plot_->xAxis, nullptr);
    plot_->axisRect()->setRangeZoomAxes(plot_->xAxis, nullptr);
    applyUtcAxisLabel();
    utc_offset_action_->setEnabled(!log_.path.isEmpty());
}

void MainWindow::showPlotContextMenu(const QPoint &pos)
{
    // Build a fresh popup each time so it reflects the current set of visible
    // streams, enabled transforms, and tag-specific controls.
    QMenu menu(this);
    // The context menu mirrors the top-level File, View, and Configuration
    // menus. Keeping the same structure avoids a long flat popup as more
    // stream/preference controls are added. Range actions are temporary here
    // because QMenu owns them only for the lifetime of this popup; the
    // persistent View -> Ranges actions are managed by range_actions_.
    QMenu *file = menu.addMenu(tr("File"));
    file->addAction(load_action_);
    QMenu *preferences = file->addMenu(tr("Preferences"));
    preferences->addAction(load_preferences_action_);
    preferences->addAction(save_preferences_action_);
    preferences->addSeparator();
    preferences->addAction(default_preferences_action_);
    file->addSeparator();
    file->addAction(print_action_);
    file->addSeparator();
    file->addAction(about_action_);

    QMenu *view = menu.addMenu(tr("View"));
    if (!stream_actions_.isEmpty()) {
        view->addAction(visible_streams_action_);
    }
    if (axis_sides_action_->isVisible()) {
        view->addAction(axis_sides_action_);
    }
    if (colors_action_->isVisible()) {
        view->addAction(colors_action_);
    }

    const QVector<SensorStream> visible = visibleStreams();
    if (!visible.isEmpty()) {
        QMenu *ranges = view->addMenu(tr("Ranges"));
        for (const SensorStream &stream : visible) {
            QAction *range_action = ranges->addAction(tr("%1 Range...").arg(stream.label));
            const QString help =
                tr("Set the displayed y-axis range for %1.").arg(stream.label);
            range_action->setToolTip(help);
            range_action->setStatusTip(help);
            range_action->setWhatsThis(help);
            range_action->setData(stream.id);
            connect(range_action, &QAction::triggered, this, [this, id = stream.id]() {
                setStreamRange(id);
            });
        }
    }
    view->addSeparator();
    view->addAction(reset_action_);
    view->addAction(zoom_to_cursors_action_);
    view->addAction(show_cursors_action_);
    if (calibration_constants_action_->isVisible()) {
        view->addSeparator();
        view->addAction(calibration_constants_action_);
    }

    QMenu *configuration = menu.addMenu(tr("Configuration"));
    configuration->addAction(edit_title_action_);
    configuration->addAction(show_title_action_);
    configuration->addSeparator();
    configuration->addAction(utc_offset_action_);
    if (sea_level_pressure_action_->isVisible()
        || activity_filter_action_->isVisible()
        || declination_action_->isVisible()
        || battery_forward_action_->isVisible()) {
        configuration->addSeparator();
        if (sea_level_pressure_action_->isVisible()) {
            configuration->addAction(sea_level_pressure_action_);
        }
        if (activity_filter_action_->isVisible()) {
            configuration->addAction(activity_filter_action_);
        }
        if (declination_action_->isVisible()) {
            configuration->addAction(declination_action_);
        }
        if (battery_forward_action_->isVisible()) {
            configuration->addAction(battery_forward_action_);
        }
    }
    menu.exec(plot_->mapToGlobal(pos));
}

QString MainWindow::streamValueAt(const SensorStream &stream, double epoch) const
{
    // Return a tooltip-ready value near the requested epoch. This is intentionally
    // nearest-at-or-after rather than interpolated, matching QCustomPlot's
    // sample-oriented interaction style.
    if (stream.time.isEmpty()) {
        return QString();
    }

    auto it = std::lower_bound(stream.time.cbegin(), stream.time.cend(), epoch);
    qsizetype index = it == stream.time.cend()
        ? stream.time.size() - 1
        : std::distance(stream.time.cbegin(), it);
    if (index < 0 || index >= stream.value.size()) {
        return QString();
    }

    return QString("%1: %2%3")
        .arg(stream.label)
        .arg(stream.value[index], 0, 'f', 2)
        .arg(unitsSuffix(stream));
}

void MainWindow::updateCompassDisplay(double epoch)
{
    // Feed the QML compass with the nearest loaded magnetic-frame sample. The
    // CompassDisplay facade applies declination/battery-forward in QML.
    if (compass_samples_.isEmpty()) {
        return;
    }

    auto it = std::lower_bound(
        compass_samples_.cbegin(),
        compass_samples_.cend(),
        epoch,
        [](const CompassDerivedSample &sample, double value) {
            return sample.epoch < value;
        });
    qsizetype index = it == compass_samples_.cend()
        ? compass_samples_.size() - 1
        : std::distance(compass_samples_.cbegin(), it);
    if (index < 0 || index >= compass_samples_.size()) {
        return;
    }

    compass_display_.showSample(compass_samples_[index]);
}

void MainWindow::showMousePosition(QMouseEvent *event)
{
    // Hovering over the plot drives both the text tooltip and, for CompassTag
    // logs, the orientation panel.
    if (metadata_box_dragging_) {
        return;
    }
    if (!plot_->axisRect()->rect().contains(event->pos())) {
        return;
    }

    const double x = timeAxis()->pixelToCoord(event->pos().x());
    QStringList parts;
    parts << formatPlotTime(x);
    for (const SensorStream &stream : visibleStreams()) {
        const QString value = streamValueAt(stream, x);
        if (!value.isEmpty()) {
            parts << value;
        }
    }
    updateCompassDisplay(x);
    QToolTip::showText(plot_->mapToGlobal(event->pos()), parts.join("\n"), this, QRect(), 3000);
}
