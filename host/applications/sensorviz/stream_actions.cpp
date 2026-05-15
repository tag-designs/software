#include "mainwindow.h"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QMessageBox>

#include <algorithm>
#include <cmath>

// Stream actions are the bridge between loaded data and the plot. Raw streams
// get checkable View actions; derived streams are controlled by their transform
// actions. The range submenu is rebuilt from visibleStreams(), so adding a new
// stream normally needs no menu-specific code.

bool MainWindow::hasStream(const QString &id) const
{
    // Convenience predicate used by transform availability checks.
    return streamById(id) != nullptr;
}

const SensorStream *MainWindow::streamById(const QString &id) const
{
    // Look up the current stream by stable id. Returned pointers are valid only
    // until streams_ is modified.
    for (const SensorStream &stream : streams_) {
        if (stream.id == id) {
            return &stream;
        }
    }
    return nullptr;
}

const SensorRecordSet *MainWindow::recordSetById(const QString &id) const
{
    // Look up a raw multi-column record set loaded by sqlite_loader.cpp.
    for (const SensorRecordSet &record_set : log_.recordSets) {
        if (record_set.id == id) {
            return &record_set;
        }
    }
    return nullptr;
}

void MainWindow::addOrReplaceStream(const SensorStream &stream, bool checked)
{
    // This path is used both for replacing a raw stream and for refreshing a
    // derived stream after its parameters change. The stream id is the stable
    // identity; labels and data can change without invalidating menu wiring.
    for (SensorStream &existing : streams_) {
        if (existing.id == stream.id) {
            existing = stream;
            QAction *action = streamActionById(stream.id);
            if (action) {
                action->setText(stream.label + unitsSuffix(stream));
                action->setChecked(checked);
            }
            refreshPlot();
            rebuildRangeActions();
            updateTransformActions();
            return;
        }
    }

    streams_.append(stream);
    if (!stream.derived) {
        addStreamAction(stream, checked);
    }
    refreshPlot();
    rebuildRangeActions();
    updateTransformActions();
}

void MainWindow::removeStream(const QString &id)
{
    // Removing a stream must also remove any custom range. Otherwise a future
    // stream with the same id, such as altitude re-enabled with new sea-level
    // pressure, could inherit a stale axis range.
    for (qsizetype i = 0; i < streams_.size(); i++) {
        if (streams_[i].id == id) {
            streams_.removeAt(i);
            break;
        }
    }

    for (qsizetype i = 0; i < stream_actions_.size(); i++) {
        QAction *action = stream_actions_[i];
        if (action->data().toString() == id) {
            visible_streams_menu_->removeAction(action);
            stream_actions_.removeAt(i);
            delete action;
            break;
        }
    }

    custom_axis_ranges_.remove(id);
    explicit_axis_ranges_.remove(id);
    refreshPlot();
    rebuildRangeActions();
    updateTransformActions();
}

void MainWindow::clearStreamActions()
{
    // Remove all View > Visible Streams actions before loading a new file.
    for (QAction *action : stream_actions_) {
        visible_streams_menu_->removeAction(action);
        delete action;
    }
    stream_actions_.clear();
    visible_streams_menu_->menuAction()->setVisible(false);
    visible_streams_menu_->setEnabled(false);
    clearRangeActions();
    updateTransformActions();
}

void MainWindow::addStreamAction(const SensorStream &stream, bool checked)
{
    // Create a persistent checkable action for one raw or generated stream.
    // The action's data stores the stable stream id used by visibleStreams().
    QAction *action = new QAction(stream.label + unitsSuffix(stream), this);
    action->setCheckable(true);
    action->setChecked(checked);
    action->setData(stream.id);
    connect(action, &QAction::toggled, this, [this]() {
        refreshPlot();
        rebuildRangeActions();
    });
    stream_actions_.append(action);
    visible_streams_menu_->addAction(action);
    visible_streams_menu_->menuAction()->setVisible(true);
    visible_streams_menu_->setEnabled(true);
}

QAction *MainWindow::streamActionById(const QString &id) const
{
    // Find the QAction that controls a stream's visibility. Used when transforms
    // need to preserve checked state while replacing stream data.
    for (QAction *action : stream_actions_) {
        if (action->data().toString() == id) {
            return action;
        }
    }
    return nullptr;
}

QVector<SensorStream> MainWindow::visibleStreams() const
{
    // Convert QAction state back into the stream list consumed by plotting,
    // range menus, tooltips, and printing.
    QVector<SensorStream> visible;
    // Raw streams are visible only when their View action is checked.
    for (QAction *action : stream_actions_) {
        if (!action->isChecked()) {
            continue;
        }
        const SensorStream *stream = streamById(action->data().toString());
        if (stream) {
            visible.append(*stream);
        }
    }
    // Derived streams are visible while they exist; their existence is
    // controlled by their transform action rather than a View action.
    for (const SensorStream &stream : streams_) {
        if (stream.derived) {
            visible.append(stream);
        }
    }
    return visible;
}

void MainWindow::clearRangeActions()
{
    // Remove persistent View > Ranges entries. Context-menu ranges are built
    // separately as temporary popup actions.
    for (QAction *action : range_actions_) {
        range_menu_->removeAction(action);
        delete action;
    }
    range_actions_.clear();
    range_menu_->menuAction()->setVisible(false);
    range_menu_->setEnabled(false);
}

void MainWindow::rebuildRangeActions()
{
    // Ranges belong to what the user can see, not to every stream loaded from
    // disk. This keeps hidden diagnostic streams from cluttering the menu and
    // automatically gives derived streams range controls when they are enabled.
    clearRangeActions();
    const QVector<SensorStream> visible = visibleStreams();
    for (const SensorStream &stream : visible) {
        addRangeAction(stream);
    }
    const bool has_ranges = !range_actions_.isEmpty();
    range_menu_->menuAction()->setVisible(has_ranges);
    range_menu_->setEnabled(has_ranges);
}

void MainWindow::addRangeAction(const SensorStream &stream)
{
    // Add one persistent range action for a currently displayed stream.
    QAction *action = new QAction(tr("%1 Range...").arg(stream.label), this);
    action->setData(stream.id);
    connect(action, &QAction::triggered, this, &MainWindow::setStreamRangeFromAction);
    range_actions_.append(action);
    range_menu_->addAction(action);
}

QCPRange MainWindow::defaultRangeForStream(const SensorStream &stream) const
{
    // Default y-axis range priority:
    // 1. fixed profile range, such as activity 0-100 or voltage 0-5;
    // 2. data-derived range with 5% padding.
    // Custom user ranges are handled by setStreamRange()/rebuildPlot() before
    // this helper is consulted.
    if (stream.axisRange.enabled) {
        return QCPRange(stream.axisRange.lower, stream.axisRange.upper);
    }

    if (stream.value.isEmpty()) {
        return QCPRange(0.0, 1.0);
    }

    const auto minmax = std::minmax_element(stream.value.cbegin(), stream.value.cend());
    double lower = *minmax.first;
    double upper = *minmax.second;
    double span = upper - lower;
    if (span <= 0.0) {
        span = std::max(1.0, std::abs((lower + upper) * 0.5) * 0.1);
        const double center = (lower + upper) * 0.5;
        lower = center - span * 0.5;
        upper = center + span * 0.5;
    }

    const double margin = span * 0.05;
    return QCPRange(lower - margin, upper + margin);
}

void MainWindow::setStreamRangeFromAction()
{
    // Slot used by persistent View > Ranges actions. QAction::data carries the
    // stream id so one slot can handle all streams.
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action) {
        return;
    }
    setStreamRange(action->data().toString());
}

void MainWindow::setStreamRange(const QString &id)
{
    // One dialog handles all scalar streams. The action stores the stream id in
    // QAction::data(), which lets both the top-level View menu and the context
    // menu call the same slot without stream-specific code.
    const SensorStream *stream = streamById(id);
    if (!stream || stream->value.isEmpty()) {
        return;
    }

    const QCPRange initial_range = custom_axis_ranges_.contains(id)
        ? custom_axis_ranges_[id]
        : defaultRangeForStream(*stream);
    double lower = initial_range.lower;
    double upper = initial_range.upper;
    const double span = std::max(1.0, upper - lower);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("%1 Range").arg(stream->label));

    QDoubleSpinBox *lower_box = new QDoubleSpinBox(&dialog);
    QDoubleSpinBox *upper_box = new QDoubleSpinBox(&dialog);
    for (QDoubleSpinBox *box : {lower_box, upper_box}) {
        box->setRange(-1.0e9, 1.0e9);
        box->setDecimals(2);
        box->setSingleStep(span / 100.0);
        if (!stream->units.isEmpty()) {
            box->setSuffix(" " + stream->units);
        }
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
        QMessageBox::warning(this, tr("Set Range"), tr("Minimum must be less than maximum."));
        return;
    }

    custom_axis_ranges_[id] = QCPRange(lower, upper);
    explicit_axis_ranges_.insert(id);

    // Altitude is derived directly from pressure. When the user changes the
    // pressure range, keep altitude aligned unless they have explicitly set an
    // altitude range of its own.
    if (id == "pressure" && hasStream("altitude") && !explicit_axis_ranges_.contains("altitude")) {
        custom_axis_ranges_["altitude"] =
            QCPRange(pressureToAltitude(upper), pressureToAltitude(lower));
    }
    if (id == "activity"
        && hasStream("activity_lowpass")
        && !explicit_axis_ranges_.contains("activity_lowpass")) {
        custom_axis_ranges_["activity_lowpass"] = QCPRange(lower, upper);
    } else if (id == "activity_lowpass"
               && hasStream("activity")
               && !explicit_axis_ranges_.contains("activity")) {
        custom_axis_ranges_["activity"] = QCPRange(lower, upper);
    }
    refreshPlot();
}
