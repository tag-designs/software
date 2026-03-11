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
#include "ui_mainwindow.h"


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
          Hcal[0] = cal[3].toFloat(nullptr);
          Hcal[1] = cal[4].toFloat(nullptr);
          Hcal[2] = cal[5].toFloat(nullptr);
          //qInfo() << "V " << Vcal[0] << Vcal[1] << Vcal[2];
        }

        if (cal[2].startsWith("A[0]:")) {
         
          if (cal.length() < 6)
            continue;
          Scal[0][0] = cal[3].toFloat(nullptr);
          Scal[0][1] = cal[4].toFloat(nullptr);
          Scal[0][2] = cal[5].toFloat(nullptr);
          //qInfo() << "A[0] " << Scal[0][0] << Scal[0][1] << Scal[0][2];
        }

        if (cal[2].startsWith("A[1]:")) { 
          if (cal.length() < 6)
            continue;
          Scal[1][0] = cal[3].toFloat(nullptr);
          Scal[1][1] = cal[4].toFloat(nullptr);
          Scal[1][2] = cal[5].toFloat(nullptr);
        }

         if (cal[2].startsWith("A[2]:")) { 
          if (cal.length() < 6)
            continue;
          Scal[2][0] = cal[3].toFloat(nullptr);
          Scal[2][1] = cal[4].toFloat(nullptr);
          Scal[2][2] = cal[5].toFloat(nullptr);
     
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

