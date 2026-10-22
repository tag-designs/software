#include "mainwindow.h"

#include <QAction>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyle>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <set>
#include <cmath>

namespace
{

void setColorSwatch(QLabel *swatch, const QColor &color)
{
    swatch->setStyleSheet(
        QString("background-color: %1; border: 1px solid #555;").arg(color.name(QColor::HexRgb)));
}

} // namespace

// Stream actions are views of the ViewModel: the stream list, its order (a
// derived stream is listed after its inputs, a rule kept in the core) and
// each non-derived stream's visibility live in view_. Range, color, and
// axis-side controls are rebuilt from visibleStreams(), keeping menus focused
// on what is currently on the plot.

bool MainWindow::hasStream(const QString &id) const
{
    // Convenience predicate used by transform availability checks.
    return view_.hasStream(id.toStdString());
}

const SensorStream *MainWindow::streamById(const QString &id) const
{
    // Look up the current stream by stable id. Returned pointers are valid only
    // until the stream list changes.
    return view_.stream(id.toStdString());
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

bool MainWindow::isStreamVisible(const QString &id) const
{
    // A transform keeps its output's visibility when it remakes it. Streams
    // without a visibility of their own (none yet, or derived) count as shown.
    const SensorStream *stream = streamById(id);
    return !stream || stream->derived || view_.isVisible(stream->id);
}

void MainWindow::addOrReplaceStream(const SensorStream &stream, bool checked)
{
    // This path is used both for replacing a raw stream and for refreshing a
    // derived stream after its parameters change. The stream id is the stable
    // identity; labels and data can change without invalidating menu wiring.
    // The transform registered the stream's plot data before calling this;
    // a replaced stream's plot data is released once the new one is in place.
    const sensorviz::ViewModel::StreamUpdate update = view_.addOrReplaceStream(stream, checked);
    if (update.released != sensordata::kNoStream && log_.data) {
        log_.data->remove(update.released);
    }
    syncStreamActions();
    if (update.replaced && (update.changes & sensorviz::ViewModel::Visibility) != 0) {
        // Remaking a stream with a different visibility redraws first, as
        // toggling its View action always has.
        streamVisibilityChanged();
    }
    refreshPlot();
    rebuildRangeActions();
    updateColorsAction();
    updateAxisSidesAction();
    updateTransformActions();
}

void MainWindow::removeStream(const QString &id)
{
    // The ViewModel also drops the stream's custom range, so a future stream
    // with the same id, such as altitude re-enabled with new sea-level
    // pressure, cannot inherit a stale axis range.
    const sensordata::StreamId data = view_.removeStream(id.toStdString());
    if (data != sensordata::kNoStream && log_.data) {
        log_.data->remove(data);
    }
    syncStreamActions();
    refreshPlot();
    rebuildRangeActions();
    updateColorsAction();
    updateAxisSidesAction();
    updateTransformActions();
}

void MainWindow::syncStreamActions()
{
    // One checkable action per non-derived stream, in stream order, mirroring
    // its visibility. They are not placed in menus; the Visible Streams dialog
    // lists them. Toggling one sets the visibility in the ViewModel.
    const std::vector<std::string> ids = view_.toggleableStreamIds();
    bool same_list = static_cast<size_t>(stream_actions_.size()) == ids.size();
    for (size_t i = 0; same_list && i < ids.size(); i++) {
        same_list = stream_actions_[static_cast<qsizetype>(i)]->data().toString()
            == toQString(ids[i]);
    }
    if (!same_list) {
        for (QAction *action : stream_actions_) {
            delete action;
        }
        stream_actions_.clear();
        for (const std::string &id : ids) {
            QAction *action = new QAction(this);
            action->setCheckable(true);
            action->setData(toQString(id));
            connect(action, &QAction::toggled, this, [this, id](bool checked) {
                if (view_.setStreamVisible(id, checked) != 0) {
                    streamVisibilityChanged();
                }
            });
            stream_actions_.append(action);
        }
    }
    for (QAction *action : stream_actions_) {
        const SensorStream *stream = streamById(action->data().toString());
        const QSignalBlocker blocker(action);
        action->setText(toQString(stream->label) + unitsSuffix(*stream));
        action->setChecked(view_.isVisible(stream->id));
    }
    if (!log_.path.isEmpty()) {
        // Before any log the command stays visible but disabled, like the
        // rest of the startup menus.
        const bool any = !stream_actions_.isEmpty();
        visible_streams_action_->setVisible(any);
        visible_streams_action_->setEnabled(any);
    }
}

void MainWindow::streamVisibilityChanged()
{
    // What changes when the set of shown streams does.
    refreshPlot();
    rebuildRangeActions();
    updateColorsAction();
    updateAxisSidesAction();
    rememberCurrentPreferences();
}

void MainWindow::applyStreamVisibility(const QSet<QString> &visible_ids)
{
    // Apply a batch of visibility edits. The plot and dependent menus are
    // rebuilt once at the end, which keeps the multi-select dialog responsive
    // for large stream lists.
    std::set<std::string> ids;
    for (const QString &id : visible_ids) {
        ids.insert(id.toStdString());
    }
    if (view_.setVisibleStreams(ids) == 0) {
        return;
    }
    syncStreamActions();
    streamVisibilityChanged();
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

    // Plain checkboxes rather than checkable QListWidget items: the Qt 6.10
    // macOS style draws an item view's check indicator on the first row only,
    // so every other stream looked unselectable.
    //
    // Streams that share an axis are listed under a group checkbox, at the
    // place of the group's first stream: checking it shows the whole group,
    // unchecking it hides it, and it shows partly checked while only some
    // members are shown. A stream alone in its group is listed on its own.
    struct StreamGroup
    {
        std::string key;               ///< sensorviz::axisGroupKey(), or empty.
        std::vector<QAction *> actions; ///< Members, in stream order.
    };
    std::vector<StreamGroup> groups;
    for (QAction *action : stream_actions_) {
        const SensorStream *stream = streamById(action->data().toString());
        const std::string key = stream ? sensorviz::axisGroupKey(*stream) : std::string();
        auto group = std::find_if(groups.begin(), groups.end(), [&key](const StreamGroup &g) {
            return !key.empty() && g.key == key;
        });
        if (group == groups.end()) {
            groups.push_back(StreamGroup{key, {action}});
        } else {
            group->actions.push_back(action);
        }
    }

    QWidget *stream_list = new QWidget;
    QVBoxLayout *stream_layout = new QVBoxLayout(stream_list);
    QVector<QCheckBox *> stream_boxes;
    stream_boxes.reserve(stream_actions_.size());
    const int indent = style()->pixelMetric(QStyle::PM_IndicatorWidth) + 8;
    for (const StreamGroup &group : groups) {
        QCheckBox *group_box = nullptr;
        QVBoxLayout *member_layout = stream_layout;
        if (group.actions.size() > 1) {
            const SensorStream *first = streamById(group.actions.front()->data().toString());
            const QString name = first->groupLabel.empty() ? toQString(first->group)
                                                           : toQString(first->groupLabel);
            group_box = new QCheckBox(name + unitsSuffix(*first), stream_list);
            group_box->setTristate(true);
            stream_layout->addWidget(group_box);
            member_layout = new QVBoxLayout;
            member_layout->setContentsMargins(indent, 0, 0, 0);
            stream_layout->addLayout(member_layout);
        }
        QVector<QCheckBox *> members;
        for (QAction *action : group.actions) {
            QCheckBox *box = new QCheckBox(action->text(), stream_list);
            box->setProperty("streamId", action->data());
            box->setChecked(action->isChecked());
            member_layout->addWidget(box);
            stream_boxes.append(box);
            members.append(box);
        }
        if (!group_box) {
            continue;
        }
        const auto sync_group = [group_box, members]() {
            const qsizetype checked = std::count_if(members.begin(), members.end(),
                                                    [](QCheckBox *box) { return box->isChecked(); });
            const QSignalBlocker blocker(group_box);
            group_box->setCheckState(checked == 0 ? Qt::Unchecked
                                     : checked == members.size() ? Qt::Checked
                                                                 : Qt::PartiallyChecked);
        };
        sync_group();
        for (QCheckBox *box : members) {
            connect(box, &QCheckBox::toggled, &dialog, sync_group);
        }
        // A click cycles a tristate box through partial; instead, a click
        // shows every member unless all are already shown.
        connect(group_box, &QCheckBox::clicked, &dialog, [members, sync_group]() {
            const bool all = std::all_of(members.begin(), members.end(),
                                         [](QCheckBox *box) { return box->isChecked(); });
            for (QCheckBox *box : members) {
                box->setChecked(!all);
            }
            sync_group();
        });
    }
    stream_layout->addStretch(1);

    QScrollArea *scroll = new QScrollArea(&dialog);
    scroll->setWidget(stream_list);
    scroll->setWidgetResizable(true);

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *select_all_button =
        buttons->addButton(tr("Select All"), QDialogButtonBox::ActionRole);
    QPushButton *clear_all_button =
        buttons->addButton(tr("Clear All"), QDialogButtonBox::ActionRole);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(scroll);
    layout->addWidget(buttons);
    dialog.setLayout(layout);

    connect(select_all_button, &QPushButton::clicked, &dialog, [stream_boxes]() {
        for (QCheckBox *box : stream_boxes) {
            box->setChecked(true);
        }
    });
    connect(clear_all_button, &QPushButton::clicked, &dialog, [stream_boxes]() {
        for (QCheckBox *box : stream_boxes) {
            box->setChecked(false);
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QSet<QString> visible_ids;
    for (const QCheckBox *box : stream_boxes) {
        if (box->isChecked()) {
            visible_ids.insert(box->property("streamId").toString());
        }
    }
    applyStreamVisibility(visible_ids);
}

QVector<SensorStream> MainWindow::visibleStreams() const
{
    // The shown streams, consumed by range menus, dialogs and tooltips.
    // The ViewModel's drawing order: shown non-derived streams, then derived
    // ones, which are visible while they exist.
    QVector<SensorStream> visible;
    for (const std::string &id : view_.visibleStreamIds()) {
        if (const SensorStream *stream = view_.stream(id)) {
            visible.append(*stream);
        }
    }
    return visible;
}

void MainWindow::updateAxisSidesAction()
{
    // The side chooser edits only visible streams, so hide it until there is at
    // least one stream on the plot.
    const bool has_visible_streams = !visibleStreams().isEmpty();
    axis_sides_action_->setVisible(has_visible_streams);
    axis_sides_action_->setEnabled(has_visible_streams);
}

void MainWindow::showAxisSidesDialog()
{
    // Batch editor for value-axis placement, one row per axis as drawn: a
    // group of streams sharing an axis is one row, so its members cannot be
    // given conflicting sides. This is intentionally viewer-only: choosing
    // left/right here does not mutate SensorStream metadata or the log.
    struct AxisRow
    {
        std::vector<std::string> members; ///< Stream ids drawn on the axis.
        QRadioButton *left = nullptr;     ///< Checked for the left side.
        QRadioButton *right = nullptr;    ///< Checked for the right side.
    };
    const sensorviz::PlotSpec &spec = plot_view_->spec();
    if (spec.axes.empty()) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Axis Sides"));

    QGridLayout *grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("Axis"), &dialog), 0, 0);
    grid->addWidget(new QLabel(tr("Axis Side"), &dialog), 0, 1);

    std::vector<AxisRow> rows;
    rows.reserve(spec.axes.size());
    for (const sensorviz::PlotAxisSpec &axis : spec.axes) {
        AxisRow row;
        for (int trace : axis.traces) {
            row.members.push_back(spec.traces[static_cast<size_t>(trace)].id);
        }
        QLabel *label = new QLabel(toQString(axis.label), &dialog);
        QWidget *side_widget = new QWidget(&dialog);
        QHBoxLayout *side_layout = new QHBoxLayout;
        side_layout->setContentsMargins(0, 0, 0, 0);
        row.left = new QRadioButton(tr("Left"), side_widget);
        row.right = new QRadioButton(tr("Right"), side_widget);
        side_layout->addWidget(row.left);
        side_layout->addWidget(row.right);
        side_layout->addStretch();
        side_widget->setLayout(side_layout);
        if (axis.side == SensorAxisSide::Right) {
            row.right->setChecked(true);
        } else {
            row.left->setChecked(true);
        }
        const int grid_row = static_cast<int>(rows.size()) + 1;
        grid->addWidget(label, grid_row, 0);
        grid->addWidget(side_widget, grid_row, 1);
        rows.push_back(row);
    }

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *defaults_button =
        buttons->addButton(tr("Defaults"), QDialogButtonBox::ResetRole);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addLayout(grid);
    layout->addWidget(buttons);
    dialog.setLayout(layout);

    // An axis's default side is its first member's, as buildPlotSpec() uses
    // when no member has an override.
    connect(defaults_button, &QPushButton::clicked, this, [this, &rows]() {
        for (const AxisRow &row : rows) {
            const SensorStream *first = view_.stream(row.members.front());
            const bool right = first && first->axisSide == SensorAxisSide::Right;
            (right ? row.right : row.left)->setChecked(true);
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    // Overrides stay per stream in preferences; every member of an axis gets
    // the axis's side, stored only where it differs from that member's default.
    std::map<std::string, SensorAxisSide> sides = view_.axisSideOverrides();
    for (const AxisRow &row : rows) {
        const SensorAxisSide chosen_side =
            row.right->isChecked() ? SensorAxisSide::Right : SensorAxisSide::Left;
        for (const std::string &id : row.members) {
            const SensorStream *stream = view_.stream(id);
            if (!stream) {
                continue;
            }
            if (chosen_side == stream->axisSide) {
                sides.erase(id);
            } else {
                sides[id] = chosen_side;
            }
        }
    }
    view_.setAxisSideOverrides(sides);

    refreshPlot();
    rememberCurrentPreferences();
}

QColor MainWindow::effectiveStreamColor(const SensorStream &stream) const
{
    // The user's override from the ViewModel, or the stream's default.
    return toQColor(view_.effectiveColor(stream.id));
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
    QAction *action = new QAction(tr("%1 Range...").arg(toQString(stream.label)), this);
    const QString help =
        tr("Set the displayed y-axis range for %1.").arg(toQString(stream.label));
    action->setToolTip(help);
    action->setStatusTip(help);
    action->setWhatsThis(help);
    action->setData(toQString(stream.id));
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

    sensorviz::PlotRange data_range;
    if (!sensorviz::plotDataRange(log_.data.get(), stream.dataId, data_range)) {
        return QCPRange(0.0, 1.0);
    }

    double lower = data_range.lower;
    double upper = data_range.upper;
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
    if (!stream || !plotDataInfo(*stream)) {
        return;
    }

    const std::string key = id.toStdString();
    const QCPRange initial_range = view_.hasCustomRange(key)
        ? QCPRange(view_.customRanges().at(key).lower, view_.customRanges().at(key).upper)
        : defaultRangeForStream(*stream);
    double lower = initial_range.lower;
    double upper = initial_range.upper;
    const double span = std::max(1.0, upper - lower);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("%1 Range").arg(toQString(stream->label)));

    QDoubleSpinBox *lower_box = new QDoubleSpinBox(&dialog);
    QDoubleSpinBox *upper_box = new QDoubleSpinBox(&dialog);
    for (QDoubleSpinBox *box : {lower_box, upper_box}) {
        box->setRange(-1.0e9, 1.0e9);
        box->setDecimals(2);
        box->setSingleStep(span / 100.0);
        if (!stream->units.empty()) {
            box->setSuffix(" " + toQString(stream->units));
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

    view_.setCustomRange(key, sensorviz::PlotRange{lower, upper}, true);

    // Streams in one axis group share one axis, which takes the first
    // member's custom range; give them all this one so the axis follows it.
    const std::string group_key = sensorviz::axisGroupKey(*stream);
    if (!group_key.empty()) {
        for (const SensorStream &member : view_.streams()) {
            if (member.id != key && sensorviz::axisGroupKey(member) == group_key) {
                view_.setCustomRange(member.id, sensorviz::PlotRange{lower, upper}, true);
            }
        }
    }

    // Altitude is derived directly from pressure. When the user changes the
    // pressure range, keep altitude aligned unless they have explicitly set an
    // altitude range of its own.
    if ((id == "pressure" || id == "imu_pressure")
        && hasStream("altitude")
        && !view_.isExplicitRange("altitude")) {
        view_.setCustomRange(
            "altitude",
            sensorviz::PlotRange{pressureToAltitude(upper), pressureToAltitude(lower)},
            false);
    }
    if (id == "activity"
        && hasStream("activity_lowpass")
        && !view_.isExplicitRange("activity_lowpass")) {
        view_.setCustomRange("activity_lowpass", sensorviz::PlotRange{lower, upper}, false);
    } else if (id == "activity_lowpass"
               && hasStream("activity")
               && !view_.isExplicitRange("activity")) {
        view_.setCustomRange("activity", sensorviz::PlotRange{lower, upper}, false);
    }
    refreshPlot();
}

void MainWindow::updateColorsAction()
{
    // The color dialog edits only visible streams, so hide the command until
    // there is something on the plot.
    const bool has_visible_streams = !visibleStreams().isEmpty();
    colors_action_->setVisible(has_visible_streams);
    colors_action_->setEnabled(has_visible_streams);
}

void MainWindow::showStreamColorsDialog()
{
    // Batch editor for display colors. Edits are staged in a local map so
    // Cancel can discard all choices, including default resets.
    const QVector<SensorStream> visible = visibleStreams();
    if (visible.isEmpty()) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Stream Colors"));

    QMap<QString, QColor> staged_colors;
    for (const auto &entry : view_.colorOverrides()) {
        staged_colors.insert(toQString(entry.first), toQColor(entry.second));
    }
    QGridLayout *grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("Stream"), &dialog), 0, 0);
    grid->addWidget(new QLabel(tr("Color"), &dialog), 0, 1);

    QVector<QPushButton *> stream_default_buttons;
    stream_default_buttons.reserve(visible.size());
    for (qsizetype i = 0; i < visible.size(); i++) {
        const SensorStream &stream = visible[i];
        const QString id = toQString(stream.id);
        const QString label_text = toQString(stream.label);
        const QColor default_color = toQColor(stream.color);
        QLabel *label = new QLabel(label_text + unitsSuffix(stream), &dialog);
        QLabel *swatch = new QLabel(&dialog);
        swatch->setFixedSize(36, 18);
        setColorSwatch(swatch, effectiveStreamColor(stream));

        QPushButton *choose_button = new QPushButton(tr("Choose..."), &dialog);
        QPushButton *default_button = new QPushButton(tr("Default"), &dialog);

        QWidget *controls = new QWidget(&dialog);
        QHBoxLayout *controls_layout = new QHBoxLayout;
        controls_layout->setContentsMargins(0, 0, 0, 0);
        controls_layout->addWidget(swatch);
        controls_layout->addWidget(choose_button);
        controls_layout->addWidget(default_button);
        controls_layout->addStretch();
        controls->setLayout(controls_layout);

        connect(choose_button, &QPushButton::clicked, this, [=, &dialog, &staged_colors]() {
            const QColor initial = staged_colors.contains(id) ? staged_colors[id] : default_color;
            const QColor color =
                QColorDialog::getColor(initial, &dialog, tr("%1 Color").arg(label_text));
            if (!color.isValid()) {
                return;
            }
            staged_colors[id] = color;
            setColorSwatch(swatch, color);
        });
        connect(default_button, &QPushButton::clicked, this, [=, &staged_colors]() {
            staged_colors.remove(id);
            setColorSwatch(swatch, default_color);
        });
        stream_default_buttons.append(default_button);

        grid->addWidget(label, int(i) + 1, 0);
        grid->addWidget(controls, int(i) + 1, 1);
    }

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *defaults_button =
        buttons->addButton(tr("Defaults"), QDialogButtonBox::ResetRole);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addLayout(grid);
    layout->addWidget(buttons);
    dialog.setLayout(layout);

    connect(defaults_button, &QPushButton::clicked, this, [stream_default_buttons]() {
        for (QPushButton *button : stream_default_buttons) {
            if (button) {
                button->click();
            }
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    std::map<std::string, sensorviz::Rgb> colors = view_.colorOverrides();
    for (const SensorStream &stream : visible) {
        const QString id = toQString(stream.id);
        if (staged_colors.contains(id)) {
            colors[stream.id] = toRgb(staged_colors[id]);
        } else {
            colors.erase(stream.id);
        }
    }
    view_.setColorOverrides(colors);

    refreshPlot();
    rememberCurrentPreferences();
}
