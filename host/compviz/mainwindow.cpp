#include <QtCore>
#include <QtGui>
#include <QVector>
#include <QPrinter>
#include <QDebug>
#include <QTextStream>
#include <qcustomplot.h>
#include <iostream>
#include <float.h>

#include "mainwindow.h"
#include "tickerdatetimeoffset.h"
#include "ui_mainwindow.h"

QTextEdit *s_textEdit = nullptr;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
  ui->setupUi(this);
  makeVisible(false);

  // Temperature/voltage and activity/acceleration occupy the same visual lanes.
  // ExclusiveOptional lets the user choose either stream, or hide both.
  vt_group = new QActionGroup(this);
  vt_group->addAction(ui->actionTemperature);
  vt_group->addAction(ui->actionVoltage);
  vt_group->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);

  aa_group = new QActionGroup(this);
  aa_group->addAction(ui->actionActivity);
  aa_group->addAction(ui->actionAcceleration);
  aa_group->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);

  connect(ui->plot, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showPlotContextMenu(QPoint)));
  connect(ui->quickWidget, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showCompassContextMenu(QPoint)));  

  // main.cpp's Qt message handler appends to this widget once the window exists.
  s_textEdit = ui->te_fileinfo;

  createGraphs();
  
  // Keep all direct QML interaction behind CompassDisplay so the rest of the
  // app only deals with compass samples and display settings.
  ui->quickWidget->setSource(QUrl("qrc:/qfi/orientation_frame/MyCompass.qml"));
  compassDisplay.setRootObject(ui->quickWidget->rootObject());

  // Two full-height vertical cursors mark the region used by Zoom to Cursors.
  left = new QCPItemLine(ui->plot);
  left->setVisible(true);
  right = new QCPItemLine(ui->plot);
  right->setVisible(true);

  // Plot movement drives both the tooltip and the QML compass orientation.
  connect(ui->plot, SIGNAL(mouseMove(QMouseEvent *)),
          SLOT(onMouseMove(QMouseEvent *)));
  connect(ui->plot, SIGNAL(mouseDoubleClick(QMouseEvent *)),
          SLOT(plot_doubleclick(QMouseEvent *)));

  connect(ui->actionCompass_Declination, &QAction::triggered, 
          this, &MainWindow::on_actionCompass_Declination);

  
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::createGraphs(){

  // On macOS/Retina, the default backing-store scaling can make exported plots
  // look different from the on-screen plot. Keep the plot buffer deterministic.
  ui->plot->setBufferDevicePixelRatio(1.0);

  QCPAxisRect *axisRect = ui->plot->axisRect(0);

  // The default y-axis is reserved for heading. Extra axes are added for the
  // scalar streams so each stream keeps its own physical units and range.
  headingAxis = ui->plot->yAxis;
  temperatureAxis = ui->plot->yAxis2;
  voltageAxis = axisRect->addAxis(QCPAxis::atRight);
  activityAxis = axisRect->addAxis(QCPAxis::atLeft);
  accelAxis = axisRect->addAxis(QCPAxis::atLeft);

  activityGraph = ui->plot->addGraph(ui->plot->xAxis, activityAxis); 
  temperatureGraph =  ui->plot->addGraph(ui->plot->xAxis, temperatureAxis);
  voltageGraph = ui->plot->addGraph(ui->plot->xAxis, voltageAxis);
  headingGraph = ui->plot->addGraph(ui->plot->xAxis, headingAxis);
  accelGraph = ui->plot->addGraph(ui->plot->xAxis, accelAxis);

  temperatureGraph->setVisible(false);
  voltageGraph->setVisible(false);
  headingGraph->setVisible(true);
  accelGraph->setVisible(false);

  // Match graph pens and axis labels so overlapping axes remain readable.
  activityGraph->setPen(QPen(Qt::darkBlue));
  temperatureGraph->setPen(QPen(Qt::red));
  voltageGraph->setPen(QPen(Qt::darkGreen));
  headingGraph->setPen(QPen(Qt::magenta));
  accelGraph->setPen(QPen(Qt::blue));

  // Time is the shared independent variable, so drag and wheel zoom are limited
  // to the x-axis. Y ranges remain fixed for easier comparison while scanning.
  ui->plot->setInteraction(QCP::Interaction::iRangeDrag);
  ui->plot->axisRect()->setRangeDrag(Qt::Horizontal);
  ui->plot->axisRect()->setRangeZoom(Qt::Horizontal);
  ui->plot->setInteraction(QCP::Interaction::iRangeZoom);

  ui->plot->xAxis->setLabel("Hour:Minute (UTC)\nMonth/Day/Year");

  activityAxis->setLabel("Activity Percent");
  activityAxis->setLabelColor(Qt::darkBlue);
  activityAxis->setRange(0,100.0);

  accelAxis->setLabel("Acceleration mg");
  accelAxis->setLabelColor(Qt::blue);
  accelAxis->setRange(800.0,1200.0);
  accelAxis->setVisible(false);
  //activityAxis->setTickLabelColor(Qt::darkBlue);

  temperatureAxis->setLabel("Temperature C");
  temperatureAxis->setLabelColor(Qt::red);
  //temperatureAxis->setTickLabelColor(Qt::red);
  temperatureAxis->setVisible(false);
  temperatureAxis->setRange(0, 50);

  voltageAxis->setLabel("Voltage");
  voltageAxis->setLabelColor(Qt::darkGreen);
  //voltageAxis->setTickLabelColor(Qt::green);
  voltageAxis->setVisible(false);
  voltageAxis->setRange(0,3.5);

  headingAxis->setLabel("Heading");
  headingAxis->setLabelColor(Qt::magenta);
  //voltageAxis->setTickLabelColor(Qt::green);
  headingAxis->setVisible(true);
  headingAxis->setRange(0,360);

  // The ticker is retained so the UTC offset menu can update it without
  // rebuilding the graph objects.
  dateTicker = QSharedPointer<QCPAxisTickerDateTime>(new QCPAxisTickerDateTime);
  dateTicker->setDateTimeFormat("hh:mm\nMM/dd/yy");
  dateTicker->setTimeZone(QTimeZone(3600*(utc_offset)));//QTimeZone::utc());

  ui->plot->xAxis->setTicker(dateTicker);

  ui->plot->setCursor(QCursor(Qt::CrossCursor));

  // Initial file-dialog location.
  path = QDir::homePath();

}

// Enable on/off

void MainWindow::makeVisible(bool visible)
{
}








