#include <QtCore>
#include <QtGui>
#include <QVector>
#include <QPrinter>
#include <QDebug>
#include <QTextStream>
#include <qcustomplot.h>
#include <iostream>
#include <FastFIR/FastFIR/qjfastfir.h>
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

  // set graphranges
  
  

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
  //ui->gb_graph->setEnabled(visible);
  //ui->gb_filterparams->setEnabled(visible);
  //ui->gb_timeoffset->setEnabled(visible);
  //ui->actionPrint->setEnabled(visible);
  //ui->actionReset->setEnabled(visible);
}


// Load a data file

void MainWindow::on_pb_load_clicked()
{
  QMessageBox msgBox;
  int i;

  QString fileName = QFileDialog::getOpenFileName(this, tr("Open Data"), path,
                                                  tr("Data Files (*.txt)"));

  if (fileName.isNull())
    return;

  QFile file(fileName);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    msgBox.setText("couldn't open:" + fileName);
    msgBox.exec();
    return;
  }

  currentfilename = fileName;
  makeVisible(true);

  // save last directory

  path = QFileInfo(fileName).path();

  // Initialize data

  QTextStream in(&file);
  voltage.clear();
  voltage_time.clear();
  temperature_time.clear();
  temperature.clear();
  accel_time.clear();
  accel.clear();
  orientation.clear();
  orientation_time.clear();
  heading.clear();
  ui->te_fileinfo->clear();
  ui->te_fileinfo->append(fileName);
  ui->plot->graph(0)->data()->clear();
  ui->plot->graph(1)->data()->clear();
  ui->plot->graph(2)->data()->clear();

  // parse the file header


  while (!in.atEnd())
  {
    QString line = in.readLine();
    if (line[0] == '#')
    {
      if (line.startsWith("# CompassType")) {
        QStringList ln = line.split(" ");
        if (ln[3] != QString("COMPASSTAG"))
        {
          qInfo() << "Not compass tag: " << line;
          file.close();
          return;
        }
      }
      if (line.startsWith("# Calibration"))
      {

        //qInfo() << line;
        
        QStringList cal = line.split(" ");
        if (cal.length() < 3) {
          //qInfo() << "too few items " << cal.length();
          continue;
        }
     
        if (cal[2].startsWith("V:")) {
         
          if (cal.length() < 6){
            //qInfo() << "too few data items " << data.length();
            continue;
          }
          Vcal[0] = cal[3].toFloat(nullptr);
          Vcal[1] = cal[4].toFloat(nullptr);
          Vcal[2] = cal[5].toFloat(nullptr);
          //qInfo() << "V " << Vcal[0] << Vcal[1] << Vcal[2];
        }

        if (cal[2].startsWith("A[0]:")) {
         
          if (cal.length() < 6)
            continue;
          Acal[0][0] = cal[3].toFloat(nullptr);
          Acal[0][1] = cal[4].toFloat(nullptr);
          Acal[0][2] = cal[4].toFloat(nullptr);
          //qInfo() << "A[0] " << Acal[0][0] << Acal[0][1] << Acal[0][2];
        }

        if (cal[2].startsWith("A[1]:")) { 
          if (cal.length() < 6)
            continue;
          Acal[1][0] = cal[3].toFloat(nullptr);
          Acal[1][1] = cal[4].toFloat(nullptr);
          Acal[1][2] = cal[5].toFloat(nullptr);
        }

         if (cal[2].startsWith("A[2]:")) { 
          if (cal.length() < 6)
            continue;
          Acal[2][0] = cal[3].toFloat(nullptr);
          Acal[2][1] = cal[4].toFloat(nullptr);
          Acal[2][2] = cal[5].toFloat(nullptr);
     
          //ui->te_fileinfo->appendPlainText("Set Calibration Constants");


        }
        continue;
      }
      if (!line.startsWith("# Page"))
        ui->te_fileinfo->append(line);
    } else {
      break;
    }
  }

  // parse data
    
  while (!in.atEnd())
  {
    QString line = in.readLine();

    // read in data

      // data line

      QStringList l1 = line.split(',');

      int timestamp = l1[0].toInt();

      // cases

      // l1[1] == 'V'
      // l1[1] == 'TC'
      // l1[1] == 'ACTIVITY'
      // l1[1] == 'AM'

      if (l1[1] == "V") {     
         // voltage
         voltage_time << timestamp;
         voltage << l1[2].toDouble();           
      } else if (l1[1] == "TC") {
        // temperature
         temperature_time << timestamp;
         temperature << l1[2].toDouble(); 
         
      } else if (l1[1] == "ACTIVITY") {
         accel_time << timestamp;
         accel << l1[2].toDouble(); 
        // activity

      } else if (l1[1] == "AM") {
        sensor s;
        s.accel.setX(l1[2].toDouble());
        s.accel.setY(l1[3].toDouble());
        s.accel.setZ(l1[4].toDouble());
        s.mag.setX(l1[5].toDouble());
        s.mag.setY(l1[6].toDouble());
        s.mag.setZ(l1[7].toDouble());
        eCompass(s.mag,s.accel,s.q,s.dip,s.field,s.mg);
        QVector3D angles = s.q.toEulerAngles();
	      if (angles[2] < 0.0) angles[2] += 360.0;
        s.pitch = angles[0];
        s.roll = angles[1];
        s.yaw = angles[2];
        orientation_time << timestamp;
        orientation << s;
        heading << std::fmod(s.yaw + declination,360.0);
      }    
  }

  file.close();

  // reset cursors and range

  if (accel_time.size())
  {
    // reset cursors
    double size = accel_time.size();

    // set range

    ui->plot->xAxis->setRange(accel_time[0], accel_time[size - 1]);
    ui->plot->xAxis->scaleRange(1.1, ui->plot->xAxis->range().center()); // 10% margin
  }


  // graph the temperature data

  temperatureGraph->setData(temperature_time,temperature,true);
  voltageGraph->setData(voltage_time,voltage,true);
  activityGraph->setData(accel_time,accel,true);
  headingGraph->setData(orientation_time,heading,true);
}









