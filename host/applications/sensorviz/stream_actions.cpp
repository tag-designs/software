#include "mainwindow.h"

#include <QAction>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// Stream actions are the bridge between loaded data and the plot. Raw streams
// get checkable View actions; derived streams are controlled by their transform
// actions. Range and color actions are rebuilt from visibleStreams(), keeping
// the menus focused on what is currently on the plot.

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
            rebuildColorActions();
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
    rebuildColorActions();
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
            stream_actions_.removeAt(i);
            delete action;
            break;
        }
    }

    custom_axis_ranges_.remove(id);
    explicit_axis_ranges_.remove(id);
    refreshPlot();
    rebuildRangeActions();
    rebuildColorActions();
    updateTransformActions();
}

void MainWindow::clearStreamActions()
{
    // Remove all raw-stream state actions before loading a new file.
    for (QAction *action : stream_actions_) {
        delete action;
    }
    stream_actions_.clear();
    visible_streams_action_->setVisible(false);
    visible_streams_action_->setEnabled(false);
    clearRangeActions();
    clearColorActions();
    updateTransformActions();
}

void MainWindow::addStreamAction(const SensorStream &stream, bool checked)
{
    // Create a persistent checkable state action for one raw stream. These
    // actions are not placed directly in menus; the Visible Streams dialog edits
    // them and visibleStreams() reads them.
    QAction *action = new QAction(stream.label + unitsSuffix(stream), this);
    action->setCheckable(true);
    action->setChecked(checked);
    action->setData(stream.id);
    connect(action, &QAction::toggled, this, [this]() {
        refreshPlot();
        rebuildRangeActions();
        rebuildColorActions();
    });
    stream_actions_.append(action);
    visible_streams_action_->setVisible(true);
    visible_streams_action_->setEnabled(true);
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

void MainWindow::applyStreamVisibility(const QSet<QString> &visible_ids)
{
    // Apply a batch of visibility edits without letting each QAction emit its
    // toggled signal. The plot and dependent menus are rebuilt once at the end,
    // which keeps the multi-select dialog responsive for large stream lists.
    bool changed = false;
    for (QAction *action : stream_actions_) {
        const bool should_be_checked = visible_ids.contains(action->data().toString());
        if (action->isChecked() == should_be_checked) {
            continue;
        }

        changed = true;
        const QSignalBlocker blocker(action);
        action->setChecked(should_be_checked);
    }

    if (!changed) {
        return;
    }

    refreshPlot();
    rebuildRangeActions();
    rebuildColorActions();
}

void MainWindow::showVisibleStreamsDialog()
{
    // Batch editor for raw stream visibility. Derived streams remain under
    // Configuration because their visibility is tied to transform parameters.
    if (stream_actions_.isEmpty()) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Visible Streams"));

    QListWidget *stream_list = new QListWidget(&dialog);
    for (QAction *action : stream_actions_) {
        QListWidgetItem *item = new QListWidgetItem(action->text(), stream_list);
        item->setData(Qt::UserRole, action->data());
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(action->isChecked() ? Qt::Checked : Qt::Unchecked);
    }

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *select_all_button =
        buttons->addButton(tr("Select All"), QDialogButtonBox::ActionRole);
    QPushButton *clear_all_button =
        buttons->addButton(tr("Clear All"), QDialogButtonBox::ActionRole);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(stream_list);
    layout->addWidget(buttons);
    dialog.setLayout(layout);

    connect(select_all_button, &QPushButton::clicked, stream_list, [stream_list]() {
        for (int row = 0; row < stream_list->count(); row++) {
            stream_list->item(row)->setCheckState(Qt::Checked);
        }
    });
    connect(clear_all_button, &QPushButton::clicked, stream_list, [stream_list]() {
        for (int row = 0; row < stream_list->count(); row++) {
            stream_list->item(row)->setCheckState(Qt::Unchecked);
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QSet<QString> visible_ids;
    for (int row = 0; row < stream_list->count(); row++) {
        QListWidgetItem *item = stream_list->item(row);
        if (item->checkState() == Qt::Checked) {
            visible_ids.insert(item->data(Qt::UserRole).toString());
        }
    }
    applyStreamVisibility(visible_ids);
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

QColor MainWindow::effectiveStreamColor(const SensorStream &stream) const
{
    // Plotting asks this helper for every graph/axis color. Keeping the
    // override lookup here avoids mutating SensorStream, which still represents
    // the defaults loaded from SQLite metadata.
    const auto it = custom_stream_colors_.constFind(stream.id);
    return it == custom_stream_colors_.cend() ? stream.color : it.value();
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
    // 1. fixed metadata range, such as activity 0-100 or voltage 0-5;
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

void MainWindow::clearColorActions()
{
    // Remove persistent View > Colors entries. The user's custom color choices
    // live in custom_stream_colors_ and are cleared only when a new file is
    // loaded, so toggled derived streams keep their color if re-enabled.
    for (QAction *action : color_actions_) {
        color_menu_->removeAction(action);
        delete action;
    }
    color_actions_.clear();
    color_menu_->menuAction()->setVisible(false);
    color_menu_->setEnabled(false);
}

void MainWindow::rebuildColorActions()
{
    // Colors belong to displayed streams. Hidden streams keep any previous
    // custom color in custom_stream_colors_, but they do not occupy menu space.
    clearColorActions();
    const QVector<SensorStream> visible = visibleStreams();
    for (const SensorStream &stream : visible) {
        addColorAction(stream);
    }
    const bool has_colors = !color_actions_.isEmpty();
    color_menu_->menuAction()->setVisible(has_colors);
    color_menu_->setEnabled(has_colors);
}

void MainWindow::addColorAction(const SensorStream &stream)
{
    // Add one persistent color action for a loaded or generated stream. The
    // stream id in QAction::data keeps the action independent of labels/units.
    QAction *action = new QAction(tr("%1 Color...").arg(stream.label), this);
    action->setData(stream.id);
    connect(action, &QAction::triggered, this, &MainWindow::setStreamColorFromAction);
    color_actions_.append(action);
    color_menu_->addAction(action);
}

void MainWindow::setStreamColorFromAction()
{
    // Slot used by persistent View > Colors actions. QAction::data carries the
    // stable stream id so one slot can serve all stream color actions.
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action) {
        return;
    }
    setStreamColor(action->data().toString());
}

void MainWindow::setStreamColor(const QString &id)
{
    // The color workflow intentionally has three exits: choose a new color,
    // return to the metadata default, or cancel without changing anything.
    const SensorStream *stream = streamById(id);
    if (!stream) {
        return;
    }

    QMessageBox prompt(this);
    prompt.setWindowTitle(tr("%1 Color").arg(stream->label));
    prompt.setText(tr("Choose a display color for %1.").arg(stream->label));
    QPushButton *choose_button =
        prompt.addButton(tr("Choose Color..."), QMessageBox::AcceptRole);
    QPushButton *default_button =
        prompt.addButton(tr("Default"), QMessageBox::ResetRole);
    QPushButton *cancel_button =
        prompt.addButton(QMessageBox::Cancel);
    prompt.exec();

    if (prompt.clickedButton() == cancel_button) {
        return;
    }

    if (prompt.clickedButton() == default_button) {
        custom_stream_colors_.remove(id);
        refreshPlot();
        return;
    }

    if (prompt.clickedButton() != choose_button) {
        return;
    }

    const QColor color = QColorDialog::getColor(
        effectiveStreamColor(*stream),
        this,
        tr("%1 Color").arg(stream->label));
    if (!color.isValid()) {
        return;
    }

    custom_stream_colors_[id] = color;
    refreshPlot();
}
