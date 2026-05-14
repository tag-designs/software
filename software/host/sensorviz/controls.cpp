#include "mainwindow.h"

#include <QAction>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPrintPreviewDialog>
#include <QSignalBlocker>
#include <QTimeZone>
#include <QToolTip>

#include <algorithm>
#include <cmath>

// Runtime behavior for sensorViz lives here. The important idea is that the UI
// is stream driven:
//
// - Raw streams are loaded from SQLite in dataloading.cpp/sqlite_loader.cpp.
// - Derived streams, such as altitude and low-pass activity, are created here.
// - Every stream has a checkable View menu action. Checked actions define what
//   refreshPlot() draws.
// - Transform and range menu items are shown only when their input stream exists.
//
// Practical debugging guide:
// - Wrong/missing menu item: check updateTransformActions() or addStreamAction().
// - Transform gives odd values: check pressureToAltitude(), lowPass(), or the
//   corresponding toggled slot.
// - Plot axes/ranges look wrong: check refreshPlot(), axisForStream(), and
//   custom_axis_ranges_.
// - Cursor or zoom behavior is wrong: check plotDoubleClick(), zoomToCursors(),
//   resetCursorsToView(), and resetZoom().
namespace
{

// Barometric altitude approximation used only for display. It is intentionally
// parameterized by sea-level pressure so the user can tune local conditions.
double pressureToAltitude(double pressure_mbar, double sea_level_mbar)
{
    return 44330.0 * (1.0 - std::pow(pressure_mbar / sea_level_mbar, 1.0 / 5.257));
}

// First-order low-pass filter for activity display. The input time vector is in
// epoch seconds, so the filter can handle uneven sample spacing.
QVector<double> lowPass(
    const QVector<double> &time,
    const QVector<double> &value,
    double time_constant_seconds)
{
    QVector<double> filtered;
    if (time.isEmpty() || value.isEmpty()) {
        return filtered;
    }

    filtered.reserve(value.size());
    double previous = value.first();
    filtered.append(previous);

    for (qsizetype i = 1; i < value.size(); i++) {
        const double dt = std::max(0.0, time[i] - time[i - 1]);
        const double alpha = time_constant_seconds > 0.0
            ? dt / (time_constant_seconds + dt)
            : 1.0;
        previous = previous + alpha * (value[i] - previous);
        filtered.append(previous);
    }

    return filtered;
}

QString unitsSuffix(const SensorStream &stream)
{
    return stream.units.isEmpty() ? QString() : " " + stream.units;
}

// Several code paths need to synchronize QAction state with stream state.
// Blocking signals prevents those synchronization updates from recursively
// calling the slots that create/remove transforms.
void setActionCheckedSilently(QAction *action, bool checked)
{
    if (!action) {
        return;
    }

    QSignalBlocker blocker(action);
    action->setChecked(checked);
}

} // namespace

// ---------------------------------------------------------------------------
// General actions and tag-dependent menu visibility
// ---------------------------------------------------------------------------

void MainWindow::showAbout()
{
    QMessageBox::about(
        this,
        tr("About sensorViz"),
        tr("sensorViz\nSQLite sensor log visualization tool"));
}

void MainWindow::updateTransformActions()
{
    const bool has_pressure = hasStream("pressure");
    const bool has_activity = hasStream("activity");
    const bool has_sensor_configuration = has_pressure || has_activity;

    // Menus are driven by available streams instead of tag type names so logs
    // from future tags automatically expose only the relevant transforms.
    pressure_range_action_->setVisible(has_pressure);
    pressure_range_action_->setEnabled(has_pressure);

    altitude_action_->setVisible(has_pressure);
    altitude_action_->setEnabled(has_pressure);

    activity_filter_action_->setVisible(has_activity);
    activity_filter_action_->setEnabled(has_activity);

    configuration_sensor_separator_->setVisible(has_sensor_configuration);
    configuration_transform_separator_->setVisible(has_pressure);

    setActionCheckedSilently(altitude_action_, hasStream("altitude"));
    setActionCheckedSilently(activity_filter_action_, hasStream("activity_lowpass"));
}

// ---------------------------------------------------------------------------
// Stream/action bookkeeping
// ---------------------------------------------------------------------------
//
// streams_ contains both raw streams from the database and derived streams
// created by transforms. stream_actions_ mirrors streams_ with checkable menu
// entries. refreshPlot() consults the actions, not a separate visibility flag,
// so one toggle path serves both top-level and context menus.

bool MainWindow::hasStream(const QString &id) const
{
    return streamById(id) != nullptr;
}

const SensorStream *MainWindow::streamById(const QString &id) const
{
    for (const SensorStream &stream : streams_) {
        if (stream.id == id) {
            return &stream;
        }
    }
    return nullptr;
}

void MainWindow::addOrReplaceStream(const SensorStream &stream, bool checked)
{
    for (SensorStream &existing : streams_) {
        if (existing.id == stream.id) {
            existing = stream;
            QAction *action = streamActionById(stream.id);
            if (action) {
                action->setText(stream.label + unitsSuffix(stream));
                action->setChecked(checked);
            }
            refreshPlot();
            updateTransformActions();
            return;
        }
    }

    // Derived streams are treated like loaded streams once created: they get a
    // checkable View action, an axis, cursor readouts, and context-menu entry.
    streams_.append(stream);
    addStreamAction(stream, checked);
    refreshPlot();
    updateTransformActions();
}

void MainWindow::removeStream(const QString &id)
{
    for (qsizetype i = 0; i < streams_.size(); i++) {
        if (streams_[i].id == id) {
            streams_.removeAt(i);
            break;
        }
    }

    for (qsizetype i = 0; i < stream_actions_.size(); i++) {
        QAction *action = stream_actions_[i];
        if (action->data().toString() == id) {
            view_menu_->removeAction(action);
            stream_actions_.removeAt(i);
            delete action;
            break;
        }
    }

    custom_axis_ranges_.remove(id);
    refreshPlot();
    updateTransformActions();
}

void MainWindow::clearStreamActions()
{
    for (QAction *action : stream_actions_) {
        view_menu_->removeAction(action);
        delete action;
    }
    stream_actions_.clear();
    updateTransformActions();
}

void MainWindow::addStreamAction(const SensorStream &stream, bool checked)
{
    QAction *action = new QAction(stream.label + unitsSuffix(stream), this);
    action->setCheckable(true);
    action->setChecked(checked);
    action->setData(stream.id);
    connect(action, &QAction::toggled, this, &MainWindow::refreshPlot);
    stream_actions_.append(action);
    view_menu_->insertAction(view_stream_separator_, action);
}

QAction *MainWindow::streamActionById(const QString &id) const
{
    for (QAction *action : stream_actions_) {
        if (action->data().toString() == id) {
            return action;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Derived streams and sensor-specific configuration
// ---------------------------------------------------------------------------
//
// Transforms are currently display-only. They do not change log_ and they are
// not written back to disk; they just add/remove derived SensorStream objects.
// New transforms should follow the same pattern:
// - guard against a missing input stream
// - ask for any parameters
// - build a SensorStream with derived=true
// - addOrReplaceStream() so the rest of the UI treats it like any other stream

void MainWindow::altitudeToggled(bool checked)
{
    if (!checked) {
        removeStream("altitude");
        return;
    }

    const SensorStream *pressure = streamById("pressure");
    if (!pressure) {
        setActionCheckedSilently(altitude_action_, false);
        return;
    }

    bool ok = false;
    const double sea_level = QInputDialog::getDouble(
        this,
        tr("Altitude"),
        tr("Sea-level pressure (mbar)"),
        sea_level_pressure_,
        800.0,
        1200.0,
        2,
        &ok);
    if (!ok) {
        setActionCheckedSilently(altitude_action_, hasStream("altitude"));
        return;
    }
    sea_level_pressure_ = sea_level;

    // Altitude is a derived pressure stream. It stays linked by convention to
    // the pressure axis range when the user changes Pressure Range.
    SensorStream altitude;
    altitude.id = "altitude";
    altitude.label = tr("Altitude");
    altitude.units = "m";
    altitude.time = pressure->time;
    altitude.color = QColor(110, 70, 180);
    altitude.derived = true;
    altitude.value.reserve(pressure->value.size());
    for (double pressure_mbar : pressure->value) {
        altitude.value.append(pressureToAltitude(pressure_mbar, sea_level_pressure_));
    }

    addOrReplaceStream(altitude, true);
}

void MainWindow::activityFilterToggled(bool checked)
{
    if (!checked) {
        removeStream("activity_lowpass");
        return;
    }

    const SensorStream *activity = streamById("activity");
    if (!activity) {
        setActionCheckedSilently(activity_filter_action_, false);
        return;
    }

    bool ok = false;
    const double seconds = QInputDialog::getDouble(
        this,
        tr("Activity Filter"),
        tr("Time constant (seconds)"),
        activity_low_pass_seconds_,
        1.0,
        86400.0,
        1,
        &ok);
    if (!ok) {
        setActionCheckedSilently(activity_filter_action_, hasStream("activity_lowpass"));
        return;
    }
    activity_low_pass_seconds_ = seconds;

    // The filter is intentionally simple and display-only. Raw activity remains
    // available as its own stream so users can compare filtered/unfiltered data.
    SensorStream filtered;
    filtered.id = "activity_lowpass";
    filtered.label = tr("Activity filter");
    filtered.units = activity->units;
    filtered.time = activity->time;
    filtered.value = lowPass(activity->time, activity->value, activity_low_pass_seconds_);
    filtered.color = QColor(20, 60, 120);
    filtered.derived = true;

    addOrReplaceStream(filtered, true);
}

void MainWindow::setPressureRange()
{
    const SensorStream *pressure = streamById("pressure");
    if (!pressure || pressure->value.isEmpty()) {
        return;
    }

    const auto minmax = std::minmax_element(pressure->value.cbegin(), pressure->value.cend());
    double lower = *minmax.first;
    double upper = *minmax.second;
    if (custom_axis_ranges_.contains("pressure")) {
        lower = custom_axis_ranges_["pressure"].lower;
        upper = custom_axis_ranges_["pressure"].upper;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Pressure Range"));

    QDoubleSpinBox *lower_box = new QDoubleSpinBox(&dialog);
    QDoubleSpinBox *upper_box = new QDoubleSpinBox(&dialog);
    for (QDoubleSpinBox *box : {lower_box, upper_box}) {
        box->setRange(0.0, 2000.0);
        box->setDecimals(2);
        box->setSingleStep(1.0);
        box->setSuffix(tr(" mbar"));
    }
    lower_box->setValue(lower);
    upper_box->setValue(upper);

    QFormLayout *form = new QFormLayout;
    form->addRow(tr("Minimum"), lower_box);
    form->addRow(tr("Maximum"), upper_box);

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addWidget(buttons);
    dialog.setLayout(form);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    lower = lower_box->value();
    upper = upper_box->value();
    if (lower >= upper) {
        QMessageBox::warning(this, tr("Pressure Range"), tr("Minimum must be less than maximum."));
        return;
    }

    custom_axis_ranges_["pressure"] = QCPRange(lower, upper);
    // Keep altitude visually comparable to pressure when both are displayed.
    const double lower_altitude = pressureToAltitude(upper, sea_level_pressure_);
    const double upper_altitude = pressureToAltitude(lower, sea_level_pressure_);
    custom_axis_ranges_["altitude"] = QCPRange(lower_altitude, upper_altitude);
    QAction *pressure_action = streamActionById("pressure");
    if (pressure_action) {
        pressure_action->setChecked(true);
    }
    refreshPlot();
}

// ---------------------------------------------------------------------------
// Plot construction and axis management
// ---------------------------------------------------------------------------
//
// QCustomPlot graphs and dynamic axes are rebuilt on every refresh. That is
// simpler than trying to mutate old graph state when stream actions change, and
// it keeps derived-stream removal straightforward.

QVector<SensorStream> MainWindow::visibleStreams() const
{
    QVector<SensorStream> visible;
    for (QAction *action : stream_actions_) {
        if (!action->isChecked()) {
            continue;
        }
        const SensorStream *stream = streamById(action->data().toString());
        if (stream) {
            visible.append(*stream);
        }
    }
    return visible;
}

void MainWindow::clearDynamicAxes()
{
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

QCPAxis *MainWindow::axisForStream(int visible_index, const SensorStream &stream)
{
    QCPAxis *axis = nullptr;
    if (visible_index == 0) {
        axis = plot_->yAxis;
    } else if (visible_index == 1) {
        axis = plot_->yAxis2;
    } else {
        // Additional streams get extra axes alternating between right and left.
        const QCPAxis::AxisType side = visible_index % 2
            ? QCPAxis::atRight
            : QCPAxis::atLeft;
        axis = plot_->axisRect()->addAxis(side);
        dynamic_axes_.append(axis);
    }

    axis->setVisible(true);
    axis->setLabel(stream.label + unitsSuffix(stream));
    axis->setLabelColor(stream.color);
    axis->setTickLabelColor(stream.color);
    axis->setBasePen(QPen(stream.color));
    axis->setTickPen(QPen(stream.color));
    axis->setSubTickPen(QPen(stream.color));
    return axis;
}

void MainWindow::refreshPlot()
{
    plot_->clearGraphs();
    clearDynamicAxes();

    const QVector<SensorStream> visible = visibleStreams();
    for (int i = 0; i < visible.size(); i++) {
        const SensorStream &stream = visible[i];
        QCPAxis *axis = axisForStream(i, stream);
        QCPGraph *graph = plot_->addGraph(plot_->xAxis, axis);
        graph->setName(stream.label);
        graph->setPen(QPen(stream.color, stream.derived ? 2 : 1));
        graph->setData(stream.time, stream.value, true);
        if (custom_axis_ranges_.contains(stream.id)) {
            axis->setRange(custom_axis_ranges_[stream.id]);
        } else {
            // Each stream owns its value axis because sensors use different
            // units and ranges.
            graph->rescaleValueAxis(true);
        }
    }

    if (!visible.isEmpty()) {
        plot_->xAxis->rescale(true);
        plot_->xAxis->scaleRange(1.05, plot_->xAxis->range().center());
        resetCursorsToView();
    }
    plot_->replot();
}

void MainWindow::resetZoom()
{
    if (plot_->graphCount() == 0) {
        return;
    }

    plot_->xAxis->rescale(true);
    for (int i = 0; i < plot_->graphCount(); i++) {
        plot_->graph(i)->rescaleValueAxis(i > 0);
    }
    plot_->xAxis->scaleRange(1.05, plot_->xAxis->range().center());
    resetCursorsToView();
    plot_->replot();
}

// ---------------------------------------------------------------------------
// Cursors, print support, UTC offset, and context menu
// ---------------------------------------------------------------------------

void MainWindow::resetCursorsToView()
{
    if (!left_cursor_ || !right_cursor_) {
        return;
    }

    left_cursor_->start->setCoords(plot_->xAxis->range().lower, QCPRange::minRange);
    left_cursor_->end->setCoords(plot_->xAxis->range().lower, QCPRange::maxRange);
    right_cursor_->start->setCoords(plot_->xAxis->range().upper, QCPRange::minRange);
    right_cursor_->end->setCoords(plot_->xAxis->range().upper, QCPRange::maxRange);
}

void MainWindow::plotDoubleClick(QMouseEvent *event)
{
    if (!left_cursor_ || !right_cursor_) {
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
    if (!left_cursor_ || !right_cursor_) {
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
    QMenu menu(this);
    // The context menu mirrors the top-level menus, after filtering actions
    // that are not meaningful for the loaded log.
    for (QAction *action : stream_actions_) {
        menu.addAction(action);
    }
    if (!stream_actions_.isEmpty()) {
        menu.addSeparator();
    }
    menu.addAction(reset_action_);
    menu.addAction(zoom_to_cursors_action_);
    menu.addAction(utc_offset_action_);
    if (pressure_range_action_->isVisible()
        || altitude_action_->isVisible()
        || activity_filter_action_->isVisible()) {
        menu.addSeparator();
        if (pressure_range_action_->isVisible()) {
            menu.addAction(pressure_range_action_);
        }
        if (altitude_action_->isVisible()) {
            menu.addAction(altitude_action_);
        }
        if (activity_filter_action_->isVisible()) {
            menu.addAction(activity_filter_action_);
        }
    }
    menu.addSeparator();
    menu.addAction(load_action_);
    menu.addAction(print_action_);
    menu.exec(plot_->mapToGlobal(pos));
}

QString MainWindow::streamValueAt(const SensorStream &stream, double epoch) const
{
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

void MainWindow::showMousePosition(QMouseEvent *event)
{
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
    QToolTip::showText(plot_->mapToGlobal(event->pos()), parts.join("\n"), this, QRect(), 3000);
}
