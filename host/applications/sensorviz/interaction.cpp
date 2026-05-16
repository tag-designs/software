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

// User interaction that depends on an existing plot lives here: cursors,
// context menus, printing, UTC offset selection, and mouse-position readout.

void MainWindow::resetCursorsToView()
{
    // Keep the two zoom cursors bracketing whatever time window is currently
    // visible. Called after plot rebuilds and full-range resets.
    if (!left_cursor_ || !right_cursor_) {
        return;
    }

    left_cursor_->setVisible(true);
    right_cursor_->setVisible(true);
    left_cursor_->start->setCoords(plot_->xAxis->range().lower, QCPRange::minRange);
    left_cursor_->end->setCoords(plot_->xAxis->range().lower, QCPRange::maxRange);
    right_cursor_->start->setCoords(plot_->xAxis->range().upper, QCPRange::minRange);
    right_cursor_->end->setCoords(plot_->xAxis->range().upper, QCPRange::maxRange);
}

void MainWindow::plotDoubleClick(QMouseEvent *event)
{
    // Mouse convention inherited from compviz: double-click moves the left
    // cursor, shift-double-click moves the right cursor.
    if (!left_cursor_ || !right_cursor_ || !left_cursor_->visible()) {
        return;
    }

    const double x = plot_->xAxis->pixelToCoord(event->pos().x());
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
        plot_->xAxis->setRange(left, right);
        plot_->replot();
    }
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
    date_ticker_->setTimeZone(QTimeZone(3600 * hours));
    date_ticker_->setTickOrigin(-3600 * hours);
    if (hours == 0) {
        plot_->xAxis->setLabel(tr("Hour:Minute (UTC)\nMonth/Day/Year"));
    } else {
        plot_->xAxis->setLabel(tr("Hour:Minute (UTC%1%2)\nMonth/Day/Year")
                                   .arg(hours > 0 ? "+" : "")
                                   .arg(hours));
    }
    plot_->replot();
}

void MainWindow::showPlotContextMenu(const QPoint &pos)
{
    // Build a fresh popup each time so it reflects the current set of visible
    // streams, enabled transforms, and tag-specific controls.
    QMenu menu(this);
    // The context menu mirrors the top-level menus, after filtering actions
    // that are not meaningful for the loaded log. Range actions are temporary
    // here because QMenu owns them only for the lifetime of this popup; the
    // persistent View -> Ranges actions are managed by range_actions_.
    if (!stream_actions_.isEmpty()) {
        menu.addAction(visible_streams_action_);
    }
    if (axis_sides_action_->isVisible()) {
        menu.addAction(axis_sides_action_);
    }
    menu.addAction(reset_action_);
    menu.addAction(zoom_to_cursors_action_);
    menu.addAction(utc_offset_action_);
    const QVector<SensorStream> visible = visibleStreams();
    if (!visible.isEmpty()) {
        menu.addSeparator();
        QMenu *ranges = menu.addMenu(tr("Ranges"));
        for (const SensorStream &stream : visible) {
            QAction *range_action = ranges->addAction(tr("%1 Range...").arg(stream.label));
            range_action->setData(stream.id);
            connect(range_action, &QAction::triggered, this, [this, id = stream.id]() {
                setStreamRange(id);
            });
        }
    }
    if (!color_actions_.isEmpty()) {
        menu.addSeparator();
        QMenu *colors = menu.addMenu(tr("Colors"));
        for (QAction *action : color_actions_) {
            colors->addAction(action);
        }
    }
    if (altitude_action_->isVisible()
        || activity_filter_action_->isVisible()
        || compass_derived_action_->isVisible()
        || declination_action_->isVisible()
        || battery_forward_action_->isVisible()) {
        menu.addSeparator();
        if (altitude_action_->isVisible()) {
            menu.addAction(altitude_action_);
        }
        if (activity_filter_action_->isVisible()) {
            menu.addAction(activity_filter_action_);
        }
        if (compass_derived_action_->isVisible()) {
            menu.addAction(compass_derived_action_);
        }
        if (declination_action_->isVisible()) {
            menu.addAction(declination_action_);
        }
        if (battery_forward_action_->isVisible()) {
            menu.addAction(battery_forward_action_);
        }
    }
    menu.addSeparator();
    menu.addAction(load_action_);
    menu.addAction(print_action_);
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
    if (!plot_->axisRect()->rect().contains(event->pos())) {
        return;
    }

    const double x = plot_->xAxis->pixelToCoord(event->pos().x());
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(qint64(x), QTimeZone(3600 * utc_offset_));
    QStringList parts;
    parts << dt.toString("MM/dd hh:mm:ss");
    for (const SensorStream &stream : visibleStreams()) {
        const QString value = streamValueAt(stream, x);
        if (!value.isEmpty()) {
            parts << value;
        }
    }
    updateCompassDisplay(x);
    QToolTip::showText(plot_->mapToGlobal(event->pos()), parts.join("\n"), this, QRect(), 3000);
}
