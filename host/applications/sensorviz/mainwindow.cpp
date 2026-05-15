#include "mainwindow.h"

#include <QAction>
#include <QFrame>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimeZone>
#include <QVBoxLayout>

QTextEdit *s_textEdit = nullptr;

// This file creates the fixed shell of the application. It deliberately avoids
// loading data or doing plot math; those responsibilities live in
// dataloading.cpp and controls.cpp. The static actions created here are reused
// by both the top-level menus and the plot context menu, so changing an action
// label, shortcut, or connection here affects both places.
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    createActions();
    createUi();
    updateTransformActions();
}

void MainWindow::createActions()
{
    menuBar()->setNativeMenuBar(false);

    // Top-level commands. Stream-specific View actions are added later by
    // dataloading.cpp because they depend on the tables present in a file.
    load_action_ = new QAction(tr("&Load"), this);
    print_action_ = new QAction(tr("&Print"), this);
    about_action_ = new QAction(tr("&About"), this);
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

    connect(load_action_, &QAction::triggered, this, &MainWindow::loadLog);
    connect(print_action_, &QAction::triggered, this, &MainWindow::printPlot);
    connect(about_action_, &QAction::triggered, this, &MainWindow::showAbout);
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

    // The menu layout follows compViz: File for file-level actions, View for
    // plot/stream visibility and Configuration for transform parameters.
    // Separators are stored when later code needs to hide empty groups.
    QMenu *file_menu = menuBar()->addMenu(tr("&File"));
    file_menu->addAction(load_action_);
    file_menu->addAction(print_action_);
    file_menu->addSeparator();
    file_menu->addAction(about_action_);

    view_menu_ = menuBar()->addMenu(tr("&View"));
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
}

void MainWindow::createUi()
{
    central_ = new QWidget(this);
    setCentralWidget(central_);

    tabs_ = new QTabWidget;
    info_ = new QTextEdit;
    plot_ = new QCustomPlot;
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
    plot_frame_layout->addWidget(plot_);
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
