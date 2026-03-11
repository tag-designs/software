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

extern "C"
{
#include "log.h"
}

// hook into the error logging system

extern void myMessageOutput(QtMsgType type, const QMessageLogContext &context,
                            const QString &msg);
extern int log_level;
QTextEdit *s_textEdit = nullptr;


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
  ui->setupUi(this);
  makeVisible(false);



  vt_group = new QActionGroup(this);
  vt_group->addAction(ui->actionTemperature);
  vt_group->addAction(ui->actionVoltage);
  vt_group->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);

  connect(ui->plot, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showPlotContextMenu(QPoint)));
  connect(ui->quickWidget, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showCompassContextMenu(QPoint)));  
  s_textEdit = ui->te_fileinfo;
  qInstallMessageHandler(myMessageOutput);
  //qInfo() << "Loading QML";

  createGraphs();
  
  ui->quickWidget->setSource(QUrl("qrc:/qfi/orientation_frame/MyCompass.qml"));
  rootObject = ui->quickWidget->rootObject();

  // connect slots

  connect(ui->plot, SIGNAL(mouseMove(QMouseEvent *)),
          SLOT(onMouseMove(QMouseEvent *)));
  connect(ui->plot, SIGNAL(mouseDoubleClick(QMouseEvent *)),
          SLOT(plot_doubleclick(QMouseEvent *)));

  connect(ui->actionCompass_Declination, &QAction::triggered, 
          this, &MainWindow::on_actionCompass_Declination);

  
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::createGraphs(){

  // configure customplot

  ui->plot->setBufferDevicePixelRatio(1.0);

  QCPAxisRect *axisRect = ui->plot->axisRect(0);

  activityAxis = ui->plot->yAxis;
  temperatureAxis = ui->plot->yAxis2;
  voltageAxis = axisRect->addAxis(QCPAxis::atRight);
  headingAxis = axisRect->addAxis(QCPAxis::atLeft);

  // create graphs

  activityGraph = ui->plot->addGraph(ui->plot->xAxis, activityAxis); 
  temperatureGraph =  ui->plot->addGraph(ui->plot->xAxis, temperatureAxis);
  voltageGraph = ui->plot->addGraph(ui->plot->xAxis, voltageAxis);
  headingGraph = ui->plot->addGraph(ui->plot->xAxis, headingAxis);

  temperatureGraph->setVisible(false);
  voltageGraph->setVisible(false);
  headingGraph->setVisible(true);

  // set colors -- make a constant

  activityGraph->setPen(QPen(Qt::blue));
  temperatureGraph->setPen(QPen(Qt::red));
  voltageGraph->setPen(QPen(Qt::darkGreen));
  headingGraph->setPen(QPen(Qt::magenta));

  // enable horizontal drag

  ui->plot->setInteraction(QCP::Interaction::iRangeDrag);
  ui->plot->axisRect()->setRangeDrag(Qt::Horizontal);

  // enable zoom (horizontal only)
  ui->plot->axisRect()->setRangeZoom(Qt::Horizontal);
  ui->plot->setInteraction(QCP::Interaction::iRangeZoom);

  // set up axes

  ui->plot->xAxis->setLabel("Hour:Minute (UTC)\nMonth/Day/Year");

  activityAxis->setLabel("Activity Percent");
  activityAxis->setLabelColor(Qt::darkBlue);
  activityAxis->setRange(0,100.0);
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

  // format time on x axis

  dateTicker = QSharedPointer<QCPAxisTickerDateTime>(new QCPAxisTickerDateTime);
  dateTicker->setDateTimeFormat("hh:mm\nMM/dd/yy");
  dateTicker->setTimeZone(QTimeZone(3600*(utc_offset)));//QTimeZone::utc());

  ui->plot->xAxis->setTicker(dateTicker);

  // set mouse cursor

  ui->plot->setCursor(QCursor(Qt::CrossCursor));
  //textItem = new QCPItemText(ui->plot); // mouse text

  // set path
  path = QDir::homePath();

}

// Enable on/off

void MainWindow::makeVisible(bool visible)
{
}










