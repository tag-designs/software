#include "mainwindow.h"

#include <QAction>
#include <QCursor>
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

// User interaction that depends on an existing plot lives here: cursor and
// print commands, context menus, UTC offset selection, and the mouse-position
// readout. The plot itself, and its mouse handling, is PlotView.

void MainWindow::zoomToCursors()
{
    plot_view_->zoomToCursors();
}

void MainWindow::setCursorsVisible(bool visible)
{
    plot_view_->setCursorsVisible(visible);
}

void MainWindow::printPlot()
{
    // Open a print preview for the current plot frame. The actual rendering is
    // delegated to renderPlot() because Qt calls it from the preview dialog.
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::NativeFormat);
    printer.setPageOrientation(QPageLayout::Landscape);
    printer.setPageSize(QPageSize(QPageSize::Letter));
    // renderPlot() repaints this printer in screen coordinates, so remember
    // the device's real resolution for sizing the plot envelope.
    print_device_dpi_ = printer.resolution();

    QPrintPreviewDialog preview(&printer, this);
    connect(&preview, &QPrintPreviewDialog::paintRequested, this, &MainWindow::renderPlot);
    preview.exec();
}

void MainWindow::renderPlot(QPrinter *printer)
{
    // PlotView draws vector output with the envelope refilled at the
    // printer's resolution; print_device_dpi_ is that resolution.
    plot_view_->render(printer, print_device_dpi_);
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
        view_.utcOffsetHours(),
        -12,
        12,
        1,
        &ok);
    if (!ok) {
        return;
    }
    applyUtcOffset(hours);
}

void MainWindow::applyUtcOffset(int hours)
{
    view_.setUtcOffsetHours(hours);
    // Only the tick labels change; PlotView applies that without a rebuild.
    refreshPlot();
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
        QDateTime::fromSecsSinceEpoch(qint64(time_seconds), QTimeZone(3600 * view_.utcOffsetHours()));
    return dt.toString("MM/dd hh:mm:ss");
}

void MainWindow::updateTimeAxisForLog()
{
    // PlotView switches between the elapsed and epoch time axes from the
    // spec; only the UTC offset command depends on it here.
    utc_offset_action_->setEnabled(
        log_.timeDomain != SensorTimeDomain::ElapsedSeconds && !log_.path.empty());
}

QMenu *MainWindow::createPlotContextMenu(QWidget *parent)
{
    // Build a fresh popup each time so it reflects the current set of visible
    // streams, enabled transforms, and tag-specific controls.
    QMenu *menu = new QMenu(parent);
    // The context menu mirrors the top-level File, View, and Configuration
    // menus. Keeping the same structure avoids a long flat popup as more
    // stream/preference controls are added. Range actions are temporary here
    // because QMenu owns them only for the lifetime of this popup; the
    // persistent View -> Ranges actions are managed by range_actions_.
    QMenu *file = menu->addMenu(tr("File"));
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

    QMenu *view = menu->addMenu(tr("View"));
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
            const QString label = toQString(stream.label);
            QAction *range_action = ranges->addAction(tr("%1 Range...").arg(label));
            const QString help = tr("Set the displayed y-axis range for %1.").arg(label);
            range_action->setToolTip(help);
            range_action->setStatusTip(help);
            range_action->setWhatsThis(help);
            range_action->setData(toQString(stream.id));
            connect(range_action, &QAction::triggered, this, [this, id = toQString(stream.id)]() {
                setStreamRange(id);
            });
        }
    }
    view->addSeparator();
    view->addAction(reset_action_);
    view->addAction(zoom_to_cursors_action_);
    view->addAction(show_cursors_action_);
    view->addAction(break_gaps_action_);
    if (calibration_constants_action_->isVisible()) {
        view->addSeparator();
        view->addAction(calibration_constants_action_);
    }

    QMenu *configuration = menu->addMenu(tr("Configuration"));
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
    return menu;
}

void MainWindow::showPlotContextMenu(const QPoint &global_pos)
{
    QMenu *menu = createPlotContextMenu(this);
    menu->exec(global_pos);
    menu->deleteLater();
}

QString MainWindow::streamValueAt(const SensorStream &stream, double epoch) const
{
    // Return a tooltip-ready value near the requested epoch. This is intentionally
    // nearest-at-or-after rather than interpolated, matching QCustomPlot's
    // sample-oriented interaction style. The graph holds only an envelope, so
    // the sample comes from the plot backend, which reads a stored stream's
    // value back from SQLite exactly.
    if (!log_.data) {
        return QString();
    }
    // Inside a gap in the data there is no value to show, unless lines are
    // being joined across gaps.
    const std::optional<sensordata::Sample> sample = log_.data->valueAt(
        stream.dataId, epoch, sensordata::Pick::AtOrAfter,
        break_gaps_action_->isChecked() ? sensordata::GapMode::Break : sensordata::GapMode::Join);
    if (!sample) {
        return QString();
    }

    return QString("%1: %2%3")
        .arg(toQString(stream.label))
        .arg(sample->value, 0, 'f', 2)
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

void MainWindow::showMousePosition(double time)
{
    // Hovering over the plot drives both the text tooltip and, for CompassTag
    // logs, the orientation panel. PlotView reports only positions over the
    // data area, and none while the metadata box is being dragged.
    QStringList parts;
    parts << formatPlotTime(time);
    for (const SensorStream &stream : visibleStreams()) {
        const QString value = streamValueAt(stream, time);
        if (!value.isEmpty()) {
            parts << value;
        }
    }
    updateCompassDisplay(time);
    QToolTip::showText(QCursor::pos(), parts.join("\n"), this, QRect(), 3000);
}
