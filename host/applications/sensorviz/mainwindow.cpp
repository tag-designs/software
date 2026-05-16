#include "mainwindow.h"

#include <QAction>
#include <QFrame>
#include <QMenu>
#include <QMenuBar>
#include <QQuickWidget>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimeZone>
#include <QUrl>
#include <QVBoxLayout>

#include "sensorui_resources.h"

QTextEdit *s_textEdit = nullptr;

// This file creates the fixed shell of the application. It deliberately avoids
// loading data or doing plot math; those responsibilities live in
// dataloading.cpp and controls.cpp. The static actions created here are reused
// by both the top-level menus and the plot context menu, so changing an action
// label, shortcut, or connection here affects both places.
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    // Construct the static shell first. Data-dependent menus are hidden until
    // loadLog() has real streams/record sets to advertise.
    createActions();
    createUi();
    updateTransformActions();
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
    print_action_ = new QAction(tr("&Print"), this);
    about_action_ = new QAction(tr("&About"), this);
    visible_streams_action_ = new QAction(tr("Visible &Streams..."), this);
    visible_streams_action_->setVisible(false);
    visible_streams_action_->setEnabled(false);
    axis_sides_action_ = new QAction(tr("&Axis Sides..."), this);
    axis_sides_action_->setVisible(false);
    axis_sides_action_->setEnabled(false);
    colors_action_ = new QAction(tr("&Colors..."), this);
    colors_action_->setVisible(false);
    colors_action_->setEnabled(false);
    reset_action_ = new QAction(tr("&Reset Zoom"), this);
    zoom_to_cursors_action_ = new QAction(tr("&Zoom to Cursors"), this);
    utc_offset_action_ = new QAction(tr("&UTC Offset"), this);
    calibration_constants_action_ = new QAction(tr("&Calibration Constants"), this);
    calibration_constants_action_->setVisible(false);
    calibration_constants_action_->setEnabled(false);
    altitude_action_ = new QAction(tr("&Altitude"), this);
    altitude_action_->setCheckable(true);
    activity_filter_action_ = new QAction(tr("Activity &Filter"), this);
    activity_filter_action_->setCheckable(true);
    declination_action_ = new QAction(tr("&Declination..."), this);
    declination_action_->setVisible(false);
    declination_action_->setEnabled(false);
    battery_forward_action_ = new QAction(tr("&Battery Forward"), this);
    battery_forward_action_->setCheckable(true);
    battery_forward_action_->setChecked(true);
    battery_forward_action_->setVisible(false);
    battery_forward_action_->setEnabled(false);

    connect(load_action_, &QAction::triggered, this, &MainWindow::loadLog);
    connect(print_action_, &QAction::triggered, this, &MainWindow::printPlot);
    connect(about_action_, &QAction::triggered, this, &MainWindow::showAbout);
    connect(
        visible_streams_action_,
        &QAction::triggered,
        this,
        &MainWindow::showVisibleStreamsDialog);
    connect(axis_sides_action_, &QAction::triggered, this, &MainWindow::showAxisSidesDialog);
    connect(colors_action_, &QAction::triggered, this, &MainWindow::showStreamColorsDialog);
    connect(reset_action_, &QAction::triggered, this, &MainWindow::resetZoom);
    connect(zoom_to_cursors_action_, &QAction::triggered, this, &MainWindow::zoomToCursors);
    connect(utc_offset_action_, &QAction::triggered, this, &MainWindow::setUtcOffset);
    connect(
        calibration_constants_action_,
        &QAction::triggered,
        this,
        &MainWindow::showCalibrationConstants);
    connect(altitude_action_, &QAction::toggled, this, &MainWindow::altitudeToggled);
    connect(activity_filter_action_, &QAction::toggled, this, &MainWindow::activityFilterToggled);
    connect(declination_action_, &QAction::triggered, this, &MainWindow::setDeclination);
    connect(battery_forward_action_, &QAction::toggled, this, &MainWindow::batteryForwardToggled);

    // The menu layout follows compViz: File for file-level actions, View for
    // plot/stream visibility and Configuration for transform parameters.
    // Separators are stored when later code needs to hide empty groups.
    QMenu *file_menu = menuBar()->addMenu(tr("&File"));
    file_menu->addAction(load_action_);
    file_menu->addAction(print_action_);
    file_menu->addSeparator();
    file_menu->addAction(about_action_);

    view_menu_ = menuBar()->addMenu(tr("&View"));
    view_menu_->addAction(visible_streams_action_);
    view_menu_->addAction(axis_sides_action_);
    view_menu_->addAction(colors_action_);
    view_stream_separator_ = view_menu_->addSeparator();
    range_menu_ = view_menu_->addMenu(tr("&Ranges"));
    range_menu_->menuAction()->setVisible(false);
    view_menu_->addAction(reset_action_);
    view_menu_->addAction(zoom_to_cursors_action_);
    view_menu_->addSeparator();
    view_menu_->addAction(calibration_constants_action_);

    configuration_menu_ = menuBar()->addMenu(tr("&Configuration"));
    configuration_menu_->addAction(utc_offset_action_);
    configuration_transform_separator_ = configuration_menu_->addSeparator();
    configuration_menu_->addAction(altitude_action_);
    configuration_menu_->addAction(activity_filter_action_);
    configuration_menu_->addAction(declination_action_);
    configuration_menu_->addAction(battery_forward_action_);

    // User-facing help for every persistent menu action lives here. Keep these
    // text fields editable and close to action construction so menu wording,
    // status-bar hints, and tooltips stay synchronized.
    const struct MenuToolTip {
        QAction *action;
        const char *text;
    } menu_tooltips[] = {
        {load_action_, QT_TR_NOOP("Open a sensorViz SQLite log file.")},
        {print_action_, QT_TR_NOOP("Print or preview the current plot.")},
        {about_action_, QT_TR_NOOP("Show sensorViz version and application information.")},
        {visible_streams_action_, QT_TR_NOOP("Choose which loaded streams are displayed.")},
        {axis_sides_action_, QT_TR_NOOP("Assign visible stream axes to the left or right side of the plot.")},
        {colors_action_, QT_TR_NOOP("Choose display colors for visible streams.")},
        {range_menu_->menuAction(), QT_TR_NOOP("Set y-axis ranges for visible streams.")},
        {reset_action_, QT_TR_NOOP("Restore the full time range and default y-axis ranges.")},
        {zoom_to_cursors_action_, QT_TR_NOOP("Zoom the time axis to the interval between the two cursors.")},
        {calibration_constants_action_, QT_TR_NOOP("Show the compass calibration constants stored in the log.")},
        {utc_offset_action_, QT_TR_NOOP("Set the UTC offset used for time-axis labels.")},
        {altitude_action_, QT_TR_NOOP("Show or hide altitude derived from pressure.")},
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

void MainWindow::createUi()
{
    // Build the two-tab layout: Plot contains QCustomPlot plus the optional
    // compass panel, File Info holds metadata and Qt log messages.
    central_ = new QWidget(this);
    setCentralWidget(central_);

    tabs_ = new QTabWidget;
    info_ = new QTextEdit;
    plot_ = new QCustomPlot;
    plot_splitter_ = new QSplitter(Qt::Horizontal);
    compass_widget_ = new QQuickWidget;
    status_ = new QLabel(tr("No log loaded"));

    // Plot tab: the frame matches the visual treatment in compViz. The plot
    // itself is configured for horizontal pan/zoom because the x-axis is time.
    info_->setReadOnly(true);
    info_->setMinimumHeight(120);
    plot_->setContextMenuPolicy(Qt::CustomContextMenu);
    plot_->setBufferDevicePixelRatio(1.0);
    plot_->setInteraction(QCP::iRangeDrag, true);
    plot_->setInteraction(QCP::iRangeZoom, true);
    plot_->axisRect()->setRangeDrag(Qt::Horizontal);
    plot_->axisRect()->setRangeZoom(Qt::Horizontal);
    plot_->xAxis->setLabel(tr("Hour:Minute (UTC)\nMonth/Day/Year"));

    initializeSensorUiResources();
    compass_widget_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    compass_widget_->setSource(QUrl("qrc:/qfi/orientation_frame/MyCompass.qml"));
    compass_widget_->setMinimumWidth(220);
    compass_widget_->setMaximumWidth(320);
    compass_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
    compass_widget_->setVisible(false);
    compass_display_.setRootObject(compass_widget_->rootObject());
    compass_display_.setDeclination(declination_degrees_);
    compass_display_.setBatteryForward(battery_forward_);

    date_ticker_.reset(new QCPAxisTickerDateTime);
    date_ticker_->setDateTimeFormat("hh:mm\nMM/dd/yy");
    date_ticker_->setTimeZone(QTimeZone::UTC);
    plot_->xAxis->setTicker(date_ticker_);

    // Cursor items exist from startup, but stay hidden until data has been
    // loaded and refreshPlot() can place them on the time axis.
    left_cursor_ = new QCPItemLine(plot_);
    left_cursor_->setVisible(false);
    left_cursor_->setPen(QPen(Qt::black, 1, Qt::DashLine));
    right_cursor_ = new QCPItemLine(plot_);
    right_cursor_->setVisible(false);
    right_cursor_->setPen(QPen(Qt::black, 1, Qt::DashLine));

    QWidget *plot_tab = new QWidget;
    QVBoxLayout *plot_layout = new QVBoxLayout;
    QFrame *plot_frame = new QFrame;
    plot_frame->setFrameShape(QFrame::StyledPanel);
    plot_frame->setFrameShadow(QFrame::Raised);
    QVBoxLayout *plot_frame_layout = new QVBoxLayout;
    plot_splitter_->addWidget(plot_);
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

    connect(plot_, &QCustomPlot::customContextMenuRequested, this, &MainWindow::showPlotContextMenu);
    connect(plot_, &QCustomPlot::mouseMove, this, &MainWindow::showMousePosition);
    connect(plot_, &QCustomPlot::mouseDoubleClick, this, &MainWindow::plotDoubleClick);

    resize(1100, 700);
    setWindowTitle(tr("sensorViz"));
}
