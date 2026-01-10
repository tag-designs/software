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


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
  ui->setupUi(this);
  makeVisible(false);

  ui->plot->setBufferDevicePixelRatio(1.0);

  // activity graph
  ui->plot->addGraph(); // ui->plot->yAxis, ui->plot->xAxis);

  // temperature graph
  ui->plot->addGraph(ui->plot->xAxis, ui->plot->yAxis2);

  // voltage graph
  ui->plot->addGraph(ui->plot->xAxis, ui->plot->yAxis2);

  // set colors -- make a constant
  ui->plot->graph(0)->setPen(QPen(Qt::blue));
  ui->plot->graph(1)->setPen(QPen(Qt::red));
  ui->plot->graph(2)->setPen(QPen(Qt::darkGreen));

  ui->plot->graph(1)->setVisible(false);
  ui->plot->graph(2)->setVisible(false);

  // set graphranges
  
  ui->plot->yAxis2->setRange(0, 50);

  // enable horizontal drag
  ui->plot->setInteraction(QCP::Interaction::iRangeDrag);
  ui->plot->axisRect()->setRangeDrag(Qt::Horizontal);

  // enable zoom (horizontal only)
  ui->plot->axisRect()->setRangeZoom(Qt::Horizontal);
  ui->plot->setInteraction(QCP::Interaction::iRangeZoom);

  // set axes
  ui->plot->xAxis->setLabel("Hour:Minute (UTC)\nMonth/Day/Year");
  ui->plot->yAxis->setLabel("Activity Percent");
  ui->plot->yAxis->setLabelColor(Qt::darkBlue);
  ui->plot->yAxis2->setLabel("Temperature C");
  ui->plot->yAxis2->setLabelColor(Qt::red);
  ui->plot->yAxis2->setVisible(false);

  // format time on x axis

  dateTicker = QSharedPointer<QCPAxisTickerDateTime>(new QCPAxisTickerDateTime);
  dateTicker->setDateTimeFormat("hh:mm\nMM/dd/yy");
  //dateTicker->setDateTimeSpec(Qt::UTC);
  dateTicker->setTimeZone(QTimeZone(3600*(ui->offsetUTC->value())));//QTimeZone::utc());

  ui->plot->xAxis->setTicker(dateTicker);

  // set mouse cursor
  ui->plot->setCursor(QCursor(Qt::CrossCursor));
  textItem = new QCPItemText(ui->plot); // mouse text

  // set path
  path = QDir::homePath();

  // cursors
  left = new QCPItemLine(ui->plot);
  left->setVisible(false);
  right = new QCPItemLine(ui->plot);
  right->setVisible(false);

  ui->quickWidget->setSource(QUrl("qrc:/qfi/orientation_frame/MyCompass.qml"));

  // connect slots

  connect(ui->plot, SIGNAL(mouseMove(QMouseEvent *)),
          SLOT(onMouseMove(QMouseEvent *)));
  connect(ui->plot, SIGNAL(mouseDoubleClick(QMouseEvent *)),
          SLOT(plot_doubleclick(QMouseEvent *)));
}

MainWindow::~MainWindow() { delete ui; }

// Enable on/off

void MainWindow::makeVisible(bool visible)
{
  ui->gb_graph->setEnabled(visible);
  ui->gb_filterparams->setEnabled(visible);
  ui->gb_timeoffset->setEnabled(visible);
  ui->actionPNG->setEnabled(visible);
  ui->actionPDF->setEnabled(visible);
  ui->actionPrint->setEnabled(visible);
  ui->actionReset->setEnabled(visible);
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
  ui->te_fileinfo->clear();
  ui->te_fileinfo->appendPlainText(fileName);
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
        ui->te_fileinfo->appendPlainText(line);
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
              
      } else if (l1[1] == "TC") {
        // temperature
         
      } else if (l1[1] == "ACTIVITY") {
        // activity

      } else if (l1[1] == "AM") {
        // orientation


      }
      /*


      switch (tagtype) {
        case BITTAG:
          if (l1.length() >= 2)
          {
            if (l1[0][0] == '\t')
            { // old style}
              // temperature/voltage or bit buckets
              if (l1[2][0] == 't')
              {
                QStringList l2 = l1[2].split(' ');
                // input temperature/voltage
                if (l2.length() > 3)
                {
                  voltage_time << (l1[1].toInt());
                  temperature_time << (l1[1].toInt());
                  temperature << (l2[1].toDouble());
                  voltage << (l2[3].toDouble());
                }
              }
              else
              {
                double time = l1[1].toDouble();
                int cnt = l1[2].toInt(nullptr, 16);
                // decompress bit buckets -- currently only works
                // for minute bucket data -- will need to be extended
                // to second buckets

                for (int i = 5; 0 < i; i--)
                {
                  accel_time << time - i * 60;
                  accel_count << ((cnt >> ((i - 1) * 6)) & 63) / 0.6;
                }
              }
            }
            else
            {
              if (l1.length() > 2 && (l1[1][0] == 'V'))
              {
                QStringList v = l1[1].split(':');
                QStringList t = l1[2].split(':');
                voltage_time << l1[0].toInt();
                temperature_time << l1[0].toInt();
                temperature << t[1].toDouble();
                voltage << v[1].toDouble();
              }
              else
              {
                QStringList dat = l1[1].split(':');
                int time = l1[0].toInt();
                // SEC data is packed
                if (dat[0].startsWith(QString("SEC")))
                {
                  int cnt = dat[1].toInt(nullptr, 16);
                  for (int i = 29; 0 <= i; i--)
                  {
                    accel_time << time - i;
                    accel_count << ((cnt >> (29 - i)) & 1) * 100.0;
                  }
                }
                if (dat[0].startsWith(QString("MIN")))
                {
                  accel_time << time;
                  accel_count << dat[1].toDouble();
                }
                if (dat[0].startsWith(QString("FOURMIN")))
                {
                  accel_time << time;
                  accel_count << dat[1].toDouble();
                }
                if (dat[0].startsWith(QString("FIVEMIN")))
                {
                  accel_time << time;
                  accel_count << dat[1].toDouble();
                }
              }
          }
        }
        break;
      } */

      // Customize UI for the Compasstypes

      // Set range to 500 hPA

  /*

      if (tagtype == BITCompass|| tagtype == BITTAGNG) {
        ui->activityRange->setMaximum(105);
        ui->activityRange->setMaximum(100);
        //ui->activityRange->setEnabled(true);
        ui->gb_GraphRange->setVisible(true);

        ui->gb_filterparams->setVisible(true);
        //ui->graphMax->setEnabled(false);
        //ui->graphMin->setEnabled(false);
        ui->frameGraphMin->setVisible(false);
        ui->frameGraphMax->setVisible(false);
        //ui->label->setVisible(true);
        ui->cb_activity->setText("Activity");
        ui->tabConfig->setTabEnabled(1,true);
       ui->plot->yAxis->setLabel("Activity Percent");
      }
       */
     
  }

  file.close();

  // reset cursors and range

  if (accel_time.size())
  {
    // reset cursors
    double size = accel_time.size();

    // set range

    ui->plot->xAxis->setRange(accel_time[0], accel_time[size - 1]);
  }


  // graph the temperature data

  if (temperature_time.size())
  {
    QVector<QCPGraphData> temperatureData(temperature_time.size());
    for (i = 0; i < temperature_time.size(); i++)
    {
      temperatureData[i].key = temperature_time[i];
      temperatureData[i].value = temperature[i];
    }
    ui->plot->graph(1)->data()->set(temperatureData);
  }

  // graph voltage data
  if (voltage_time.size())
  {
    QVector<QCPGraphData> voltageData(voltage_time.size());
    for (i = 0; i < voltage_time.size(); i++)
    {
      voltageData[i].key = voltage_time[i];
      voltageData[i].value = voltage[i];
    }
    ui->plot->graph(2)->data()->set(voltageData);
  }
  on_sb_cutoff_valueChanged(ui->sb_cutoff->value());
  //on_cb_filter_low_pass_toggled(ui->cb_filter_low_pass->isChecked());
}



/*
 *  Cursors  -- handle clicks and spinboxes
 */

void MainWindow::plot_doubleclick(QMouseEvent *event)
{
  QPoint point = event->pos();
  double x = ui->plot->xAxis->pixelToCoord(point.x());
  if (left && right)
  {
    if ((x < right->start->coords().x()) &&
        (event->button() & Qt::LeftButton))
    {
    
    }

    if ((x > left->start->coords().x()) &&
        (event->button() & Qt::RightButton))
    {
    
    }
  }
}

void MainWindow::on_pb_cursor_zoom_clicked()
{
  if (left && right)
  {
    ui->plot->xAxis->setRange(left->start->coords().x(),
                              right->start->coords().x());
    ui->plot->replot();
  }
}

void MainWindow::on_cb_cursors_visible_clicked(bool checked)
{
  if (left && right)
  {
    left->setVisible(checked);
    right->setVisible(checked);
    ui->plot->replot();
  }
}






