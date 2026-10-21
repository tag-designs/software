#include "mainwindow.h"

#include <QSignalBlocker>

#include <QAction>
#include <QFrame>
#include <QMenu>
#include <QMenuBar>
#include <QQuickWidget>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "sensorui_resources.h"

QTextEdit *s_textEdit = nullptr;

// This file creates the fixed shell of the application. It deliberately avoids
// loading data or doing plot math; those responsibilities live in
// dataloading.cpp and controls.cpp. The static actions created here are reused
// by both the top-level menus and the plot context menu, so changing an action
// label, shortcut, or connection here affects both places.
MainWindow::MainWindow(const MainWindowOptions &options, QWidget *parent)
    : QMainWindow(parent),
      options_(options)
{
    // Construct the static shell first. Data-dependent menus are hidden until
    // loadLog() has real streams/record sets to advertise.
    createActions();
    createUi();
    // Menus are views of the ViewModel: whenever it changes, every action's
    // checked state is set from it.
    view_.setListener([this](sensorviz::ViewModel::Changes) { syncActions(); });
    syncActions();
    updateTransformActions();

    if (!options_.loadLogPath.isEmpty()
        || !options_.loadPreferencesPath.isEmpty()
        || !options_.captureScreenshot.isEmpty()
        || !options_.captureSuite.isEmpty()) {
        QTimer::singleShot(0, this, &MainWindow::runDocumentationStartup);
    }
}

void MainWindow::createActions()
{
    // Create persistent QAction objects once so top-level menus and context
    // menus can share checked state. Stream-specific actions are added later by
    // stream_actions.cpp after each load.
    menuBar()->setNativeMenuBar(false);

    // Top-level commands. Stream-specific View actions are added later by
    // dataloading.cpp because they depend on the tables present in a file.
    load_action_ = new QAction(tr("&Load"), this);
    load_preferences_action_ = new QAction(tr("&Load..."), this);
    load_preferences_action_->setEnabled(false);
    save_preferences_action_ = new QAction(tr("&Store..."), this);
    save_preferences_action_->setEnabled(false);
    default_preferences_action_ = new QAction(tr("Load &Defaults"), this);
    default_preferences_action_->setEnabled(false);
    print_action_ = new QAction(tr("&Print"), this);
    print_action_->setEnabled(false);
    about_action_ = new QAction(tr("&About"), this);
    user_guide_action_ = new QAction(tr("SensorViz &User Guide"), this);
    edit_title_action_ = new QAction(tr("Graph &Title..."), this);
    edit_title_action_->setEnabled(false);
    show_title_action_ = new QAction(tr("Show Graph T&itle"), this);
    show_title_action_->setCheckable(true);
    show_title_action_->setChecked(false);
    show_title_action_->setEnabled(false);
    visible_streams_action_ = new QAction(tr("Visible &Streams..."), this);
    visible_streams_action_->setEnabled(false);
    axis_sides_action_ = new QAction(tr("&Axis Sides..."), this);
    axis_sides_action_->setEnabled(false);
    colors_action_ = new QAction(tr("&Colors..."), this);
    colors_action_->setEnabled(false);
    reset_action_ = new QAction(tr("&Reset Zoom"), this);
    reset_action_->setEnabled(false);
    zoom_to_cursors_action_ = new QAction(tr("&Zoom to Cursors"), this);
    zoom_to_cursors_action_->setEnabled(false);
    show_cursors_action_ = new QAction(tr("Show &Cursors"), this);
    show_cursors_action_->setCheckable(true);
    show_cursors_action_->setChecked(true);
    show_cursors_action_->setEnabled(false);
    break_gaps_action_ = new QAction(tr("&Break Lines at Gaps"), this);
    break_gaps_action_->setCheckable(true);
    break_gaps_action_->setChecked(true);
    break_gaps_action_->setEnabled(false);
    utc_offset_action_ = new QAction(tr("&UTC Offset"), this);
    utc_offset_action_->setEnabled(false);
    calibration_constants_action_ = new QAction(tr("&Calibration Constants"), this);
    calibration_constants_action_->setVisible(false);
    calibration_constants_action_->setEnabled(false);
    sea_level_pressure_action_ = new QAction(tr("Sea-level &Pressure..."), this);
    sea_level_pressure_action_->setVisible(false);
    sea_level_pressure_action_->setEnabled(false);
    activity_filter_action_ = new QAction(tr("Activity &Filter"), this);
    activity_filter_action_->setCheckable(true);
    activity_filter_action_->setVisible(false);
    activity_filter_action_->setEnabled(false);
    declination_action_ = new QAction(tr("&Declination..."), this);
    declination_action_->setVisible(false);
    declination_action_->setEnabled(false);
    battery_forward_action_ = new QAction(tr("&Battery Forward"), this);
    battery_forward_action_->setCheckable(true);
    battery_forward_action_->setChecked(true);
    battery_forward_action_->setVisible(false);
    battery_forward_action_->setEnabled(false);

    connect(load_action_, &QAction::triggered, this, &MainWindow::loadLog);
    connect(load_preferences_action_, &QAction::triggered, this, &MainWindow::loadPreferences);
    connect(save_preferences_action_, &QAction::triggered, this, &MainWindow::savePreferences);
    connect(
        default_preferences_action_,
        &QAction::triggered,
        this,
        &MainWindow::loadDefaultPreferences);
    connect(print_action_, &QAction::triggered, this, &MainWindow::printPlot);
    connect(about_action_, &QAction::triggered, this, &MainWindow::showAbout);
    connect(user_guide_action_, &QAction::triggered, this, &MainWindow::showUserGuide);
    connect(edit_title_action_, &QAction::triggered, this, &MainWindow::editGraphTitle);
    connect(show_title_action_, &QAction::toggled, this, &MainWindow::setGraphTitleVisible);
    connect(
        visible_streams_action_,
        &QAction::triggered,
        this,
        &MainWindow::showVisibleStreamsDialog);
    connect(axis_sides_action_, &QAction::triggered, this, &MainWindow::showAxisSidesDialog);
    connect(colors_action_, &QAction::triggered, this, &MainWindow::showStreamColorsDialog);
    connect(reset_action_, &QAction::triggered, this, &MainWindow::resetZoom);
    connect(zoom_to_cursors_action_, &QAction::triggered, this, &MainWindow::zoomToCursors);
    connect(show_cursors_action_, &QAction::toggled, this, &MainWindow::setCursorsVisible);
    connect(break_gaps_action_, &QAction::toggled, this, [this](bool checked) {
        // A display choice that follows the tag type, like colors and sides.
        view_.setBreakAtGaps(checked);
        refreshPlot();
        rememberCurrentPreferences();
    });
    connect(utc_offset_action_, &QAction::triggered, this, &MainWindow::setUtcOffset);
    connect(
        calibration_constants_action_,
        &QAction::triggered,
        this,
        &MainWindow::showCalibrationConstants);
    connect(sea_level_pressure_action_, &QAction::triggered, this, &MainWindow::setSeaLevelPressure);
    connect(activity_filter_action_, &QAction::toggled, this, &MainWindow::activityFilterToggled);
    connect(declination_action_, &QAction::triggered, this, &MainWindow::setDeclination);
    connect(battery_forward_action_, &QAction::toggled, this, &MainWindow::batteryForwardToggled);

    // The menu layout follows compViz: File for file-level actions, View for
    // plot/stream visibility and Configuration for transform parameters.
    // Separators are stored when later code needs to hide empty groups.
    file_menu_ = menuBar()->addMenu(tr("&File"));
    file_menu_->addAction(load_action_);
    preferences_menu_ = file_menu_->addMenu(tr("&Preferences"));
    preferences_menu_->addAction(load_preferences_action_);
    preferences_menu_->addAction(save_preferences_action_);
    preferences_menu_->addSeparator();
    preferences_menu_->addAction(default_preferences_action_);
    file_menu_->addSeparator();
    file_menu_->addAction(print_action_);

    view_menu_ = menuBar()->addMenu(tr("&View"));
    view_menu_->addAction(visible_streams_action_);
    view_menu_->addAction(axis_sides_action_);
    view_menu_->addAction(colors_action_);
    range_menu_ = view_menu_->addMenu(tr("&Ranges"));
    range_menu_->setEnabled(false);
    view_stream_separator_ = view_menu_->addSeparator();
    view_menu_->addAction(reset_action_);
    view_menu_->addAction(zoom_to_cursors_action_);
    view_menu_->addAction(show_cursors_action_);
    view_menu_->addAction(break_gaps_action_);
    view_menu_->addSeparator();
    view_menu_->addAction(calibration_constants_action_);

    configuration_menu_ = menuBar()->addMenu(tr("&Configuration"));
    configuration_menu_->addAction(edit_title_action_);
    configuration_menu_->addAction(show_title_action_);
    configuration_menu_->addSeparator();
    configuration_menu_->addAction(utc_offset_action_);
    configuration_transform_separator_ = configuration_menu_->addSeparator();
    configuration_menu_->addAction(sea_level_pressure_action_);
    configuration_menu_->addAction(activity_filter_action_);
    configuration_menu_->addAction(declination_action_);
    configuration_menu_->addAction(battery_forward_action_);

    help_menu_ = menuBar()->addMenu(tr("&Help"));
    help_menu_->addAction(user_guide_action_);
    help_menu_->addSeparator();
    help_menu_->addAction(about_action_);

    // User-facing help for every persistent menu action lives here. Keep these
    // text fields editable and close to action construction so menu wording,
    // status-bar hints, and tooltips stay synchronized.
    const struct MenuToolTip {
        QAction *action;
        const char *text;
    } menu_tooltips[] = {
        {load_action_, QT_TR_NOOP("Open a sensorViz SQLite log file.")},
        {load_preferences_action_, QT_TR_NOOP("Load saved stream visibility, color, and axis-side preferences.")},
        {save_preferences_action_, QT_TR_NOOP("Store stream visibility, color, and axis-side preferences as formatted JSON.")},
        {default_preferences_action_, QT_TR_NOOP("Restore sensorViz default visibility, colors, and axis sides for the current tag type.")},
        {print_action_, QT_TR_NOOP("Print or preview the current plot.")},
        {user_guide_action_, QT_TR_NOOP("Open the SensorViz user guide.")},
        {about_action_, QT_TR_NOOP("Show sensorViz version and application information.")},
        {edit_title_action_, QT_TR_NOOP("Edit the plot title shown above the graph.")},
        {show_title_action_, QT_TR_NOOP("Show or hide the plot title.")},
        {visible_streams_action_, QT_TR_NOOP("Choose which loaded streams are displayed.")},
        {axis_sides_action_, QT_TR_NOOP("Assign visible stream axes to the left or right side of the plot.")},
        {colors_action_, QT_TR_NOOP("Choose display colors for visible streams.")},
        {range_menu_->menuAction(), QT_TR_NOOP("Set y-axis ranges for visible streams.")},
        {reset_action_, QT_TR_NOOP("Restore the full time range and default y-axis ranges.")},
        {zoom_to_cursors_action_, QT_TR_NOOP("Zoom the time axis to the interval between the two cursors.")},
        {show_cursors_action_, QT_TR_NOOP("Show or hide the two time cursors; double-click the plot to show them again.")},
        {break_gaps_action_, QT_TR_NOOP("Break plotted lines where the data has a gap, instead of joining the samples on either side.")},
        {calibration_constants_action_, QT_TR_NOOP("Show the compass calibration constants stored in the log.")},
        {utc_offset_action_, QT_TR_NOOP("Set the UTC offset used for time-axis labels.")},
        {sea_level_pressure_action_, QT_TR_NOOP("Set mean sea-level pressure used to derive altitude.")},
        {activity_filter_action_, QT_TR_NOOP("Show or hide a low-pass filtered activity stream.")},
        {declination_action_, QT_TR_NOOP("Set magnetic declination for true-heading display.")},
        {battery_forward_action_, QT_TR_NOOP("Toggle whether the tag battery end points forward in heading display.")},
    };
    for (const MenuToolTip &entry : menu_tooltips) {
        const QString text = tr(entry.text);
        entry.action->setToolTip(text);
        entry.action->setStatusTip(text);
        entry.action->setWhatsThis(text);
    }
}

void MainWindow::updateLoadedStateActions()
{
    // Startup should be discoverable but inert: only Load and About are useful
    // before a log exists. After a load, general plot/preference controls are
    // enabled here while tag-specific controls remain governed by the stream
    // availability helpers.
    const bool has_log = !log_.path.isEmpty();
    load_preferences_action_->setEnabled(has_log);
    save_preferences_action_->setEnabled(has_log);
    default_preferences_action_->setEnabled(has_log);
    print_action_->setEnabled(has_log);
    reset_action_->setEnabled(has_log);
    zoom_to_cursors_action_->setEnabled(has_log);
    show_cursors_action_->setEnabled(has_log);
    break_gaps_action_->setEnabled(has_log);
    utc_offset_action_->setEnabled(has_log);
    edit_title_action_->setEnabled(has_log);
    show_title_action_->setEnabled(has_log);
}

void MainWindow::createUi()
{
    // Build the two-tab layout: Plot contains the PlotView plus the optional
    // compass panel, File Info holds metadata and Qt log messages.
    central_ = new QWidget(this);
    setCentralWidget(central_);

    tabs_ = new QTabWidget;
    info_ = new QTextEdit;
    plot_view_ = new PlotView;
    plot_splitter_ = new QSplitter(Qt::Horizontal);
    compass_widget_ = new QQuickWidget;
    status_ = new QLabel(tr("No log loaded"));

    // Plot tab: the frame matches the visual treatment in compViz.
    info_->setReadOnly(true);
    info_->setMinimumHeight(120);
    initializeSensorUiResources();
    compass_widget_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    compass_widget_->setSource(QUrl("qrc:/qfi/orientation_frame/MyCompass.qml"));
    compass_widget_->setMinimumWidth(220);
    compass_widget_->setMaximumWidth(320);
    compass_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
    compass_widget_->setVisible(false);
    compass_display_.setRootObject(compass_widget_->rootObject());
    compass_display_.setDeclination(view_.declinationDegrees());
    compass_display_.setBatteryForward(view_.batteryForward());

    QWidget *plot_tab = new QWidget;
    QVBoxLayout *plot_layout = new QVBoxLayout;
    QFrame *plot_frame = new QFrame;
    plot_frame->setFrameShape(QFrame::StyledPanel);
    plot_frame->setFrameShadow(QFrame::Raised);
    QVBoxLayout *plot_frame_layout = new QVBoxLayout;
    plot_splitter_->addWidget(plot_view_);
    plot_splitter_->addWidget(compass_widget_);
    plot_splitter_->setStretchFactor(0, 1);
    plot_splitter_->setStretchFactor(1, 0);
    plot_frame_layout->addWidget(plot_splitter_);
    plot_frame->setLayout(plot_frame_layout);
    plot_layout->addWidget(plot_frame);
    plot_tab->setLayout(plot_layout);

    QWidget *file_info_tab = new QWidget;
    QVBoxLayout *file_info_layout = new QVBoxLayout;
    file_info_layout->addWidget(info_);
    file_info_tab->setLayout(file_info_layout);

    tabs_->addTab(plot_tab, tr("Plot"));
    tabs_->addTab(file_info_tab, tr("File Info"));

    // The global message handler in main.cpp writes to s_textEdit. This pointer
    // is set only after the File Info widget exists, so early startup messages
    // are intentionally ignored.
    QVBoxLayout *main_layout = new QVBoxLayout;
    main_layout->addWidget(tabs_);
    central_->setLayout(main_layout);
    statusBar()->addWidget(status_, 1);
    s_textEdit = info_;

    connect(plot_view_, &PlotView::contextMenuRequested, this, &MainWindow::showPlotContextMenu);
    connect(plot_view_, &PlotView::hoverTime, this, &MainWindow::showMousePosition);
    connect(plot_view_, &PlotView::cursorsVisibleChanged, this, [this](bool visible) {
        // Double-click shows hidden cursors; the View action follows the
        // ViewModel through syncActions().
        view_.setCursorsVisible(visible);
    });

    resize(1100, 700);
    setWindowTitle(tr("sensorViz"));
}

void MainWindow::syncActions()
{
    // The one place menus take their check states from: the ViewModel. Signals
    // are blocked, so this never feeds back into the setters.
    const auto set_checked = [](QAction *action, bool checked) {
        const QSignalBlocker blocker(action);
        action->setChecked(checked);
    };
    set_checked(show_title_action_, view_.titleVisible());
    set_checked(show_cursors_action_, view_.cursorsVisible());
    set_checked(break_gaps_action_, view_.breakAtGaps());
    set_checked(battery_forward_action_, view_.batteryForward());
    set_checked(activity_filter_action_, view_.hasStream("activity_lowpass"));
    declination_action_->setText(
        tr("&Declination... (%1 deg)").arg(view_.declinationDegrees(), 0, 'f', 2));
    sea_level_pressure_action_->setText(
        tr("Sea-level &Pressure... (%1 mbar)").arg(view_.seaLevelPressure(), 0, 'f', 2));
    syncStreamActions();
}
