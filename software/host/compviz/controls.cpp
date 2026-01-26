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
#include "constants_dialog.h"

// Track mouse in plot and display time of location

void MainWindow::onMouseMove(QMouseEvent *event)
{
  QCustomPlot *customPlot = qobject_cast<QCustomPlot *>(sender());
  double x = ui->plot->xAxis->pixelToCoord(event->pos().x());
  double y = activityAxis->pixelToCoord(event->pos().y());
  double t = temperatureAxis->pixelToCoord(event->pos().y());
  double v = voltageAxis->pixelToCoord(event->pos().y());
  double h = headingAxis->pixelToCoord(event->pos().y());
  //double v = customPlot->yAxis3->pixelToCoord(event->pos().y());

  

  QDateTime dt = QDateTime::fromSecsSinceEpoch(qint64(x),QTimeZone(3600*(utc_offset)));//QTimeZone::utc());
  QString s;
  QTextStream out(&s);

  // Generate data for mouseover string

  int index = activityGraph->findBegin(x);
  if (index) {
    y = activityGraph->dataMainValue(index);
  }

  out << "(" << dt.toString("MM/dd hh:mm");
  out << QString(", %1").arg(y,0,'f',1);

  if (ui->actionTemperature->isChecked()){
     index = temperatureGraph->findBegin(x);
     if (index) {
        t = temperatureGraph->dataMainValue(index);
    }
    out << QString(", %1").arg(t,0,'f',1);
    out << "C";
  }

  if (ui->actionVoltage->isChecked()){
     index = voltageGraph->findBegin(x);
     if (index) {
        v = voltageGraph->dataMainValue(index);
    }
    out << QString(", %1").arg(v,0,'f',1);
    out << "V";
  }

  
    index = headingGraph->findBegin(x);
  if (index) {
    h = headingGraph->dataMainValue(index);
    out << QString(", %1").arg(h,0,'f',1);
    sensor s = orientation[index];
    QMetaObject::invokeMethod(rootObject, "setOrientation",
        Q_ARG(QVariant, s.yaw),
        Q_ARG(QVariant, s.pitch),
        Q_ARG(QVariant, s.roll),
        Q_ARG(QVariant, s.dip),
        Q_ARG(QVariant, s.field),
        Q_ARG(QVariant, s.mg));
  }
  out << ")";

  // write mouse over string

  textItem->setText(s);
  textItem->position->setCoords(QPointF(x, y + 5));
  textItem->setFont(QFont(font().family(), 12));

  customPlot->replot(); //QCustomPlot::rpQueuedReplot);
}

// Set Visibility on graphs

void MainWindow::on_cb_activity_clicked(bool checked)
{
  ui->plot->graph(0)->setVisible(checked);
  ui->plot->yAxis->setVisible(checked);
  ui->plot->replot();
}

// Redraw full graph

void MainWindow::on_pb_redraw_clicked()
{
  ui->plot->xAxis->setRange(accel_time[0], accel_time[accel_time.size() - 1]);
  ui->plot->replot();
}

// Pop up About window

void MainWindow::on_actionAbout_triggered()
{
  QMessageBox msgBox;
  msgBox.setText("Compass CompassVisualization Tool\nWritten by Geoffrey Brown\n2026");
  msgBox.exec();
}

void MainWindow::on_sb_cutoff_valueChanged(double freq)
{
  const int filter_delay = 50;
  if (accel.size() > filter_delay)
  {

    // warning -- switched to slow fir because fast fir 
    // introduced unpredictable delay
    
    QVector<double> filterData(accel);
    QJSlowFIRFilter *fastfir;

    //create FastFIR filter
    fastfir = new QJSlowFIRFilter(this);
    fastfir->setKernel(QJFilterDesign::LowPassHanning(ui->sb_cutoff->value(), 1.0, 2*filter_delay+1));
    fastfir->Update(filterData);
    delete fastfir;

    accel_time_filtered.resize(filterData.size() - filter_delay);
    accel_filtered.resize(filterData.size() - filter_delay);

    for (int i = 0; i < accel_time_filtered.size(); i++) {
      accel_time_filtered[i] = accel_time[i];
      accel_filtered[i] = filterData[i+filter_delay];
    }
    on_cb_filter_low_pass_toggled(ui->cb_filter_low_pass->isChecked());
  }
}

void MainWindow::on_actionVoltage_triggered(bool checked)
{
    if (checked) {
        voltageGraph->setVisible(true);
        voltageAxis->setVisible(true);
        temperatureGraph->setVisible(false);
        temperatureAxis->setVisible(false);
    } else {
        voltageGraph->setVisible(false);
        voltageAxis->setVisible(false);
    }
    ui->plot->replot();
}

void MainWindow::on_actionTemperature_triggered(bool checked)
{
    if (checked) {
        temperatureGraph->setVisible(true);
        temperatureAxis->setVisible(true);
        voltageGraph->setVisible(false);
        voltageAxis->setVisible(false);
    } else {
        temperatureGraph->setVisible(false);
        temperatureAxis->setVisible(false);
    }
    ui->plot->replot();
}

void MainWindow::on_actionHeading_triggered(bool checked)
{
    if (checked) {
        headingGraph->setVisible(true);
        headingAxis->setVisible(true);
    } else {
        headingGraph->setVisible(false);
        headingAxis->setVisible(false);
    }
    ui->plot->replot();
}

/*
void MainWindow::on_actionEnable_Filter_triggered(bool checked)
{
    on_cb_filter_low_pass_toggled(checked);
}
    */

void MainWindow::on_cb_filter_low_pass_toggled(bool checked)
{
  ui->plot->graph(0)->setBrush(QBrush(QColor(0, 0, 0, 0)));
  if (!accel_time.size())
    return;
  if (checked) /* filter */
  {
      QVector<QCPGraphData> accelData(accel_time_filtered.size());
      for (int i = 0; i <accel_time_filtered.size(); i++)
      {
        accelData[i].value = accel_filtered[i];
        accelData[i].key = accel_time_filtered[i];
      }
      //ui->plot->yAxis->setRange(0, 25);
      ui->plot->graph(0)->data()->set(accelData);
  }
  else
  {
    QVector<QCPGraphData> accelData(accel_time.size());
    for (int i = 0; i < accel_time.size(); i++)
    {
      accelData[i].value = accel[i];
      accelData[i].key = accel_time[i];
    }
    ui->plot->graph(0)->data()->set(accelData);
  }
  ui->plot->replot();
}

// Print current graph

void MainWindow::on_actionPrint_triggered()
{
  QPrinter printer(QPrinter::HighResolution);
  printer.setOutputFormat(QPrinter::NativeFormat); 
  printer.setPageOrientation(QPageLayout::Landscape);
  printer.setPageSize(QPageSize(QPageSize::Letter));
  QPrintPreviewDialog previewDialog(&printer, this);
  connect(&previewDialog, SIGNAL(paintRequested(QPrinter *)),
          SLOT(renderPlot(QPrinter *)));
  previewDialog.exec();
}

void MainWindow::renderPlot(QPrinter *printer)
{
  
  QCPPainter painter(printer);

  const auto pageLayout = printer->pageLayout();
  const auto pageRect = pageLayout.paintRectPixels(printer->resolution());
  const auto paperRect = pageLayout.fullRectPixels(printer->resolution());
  double xscale = pageRect.width() / double(ui->plotWindow->width());
  double yscale = pageRect.height() / double(ui->plotWindow->height());
  double scale = qMin(xscale, yscale);
  //painter.translate(pageRect.x() + paperRect.width() / 2.,
   //                 pageRect.y() + paperRect.height() / 2.);
  painter.scale(scale, scale);
  painter.setMode(QCPPainter::pmVectorized);
  painter.setMode(QCPPainter::pmNoCaching);
  painter.setMode(QCPPainter::pmNonCosmetic); 

  //painter.scale(scale, scale);
  ui->plotWindow->render(&painter);//, plotWidth, plotHeight);
}

void MainWindow::on_actionCompass_Declination(){
    bool ok;
    double tmp = QInputDialog::getDouble(this, "Compass Declination", "Declination", 
                                            declination, -180.0, 180.0, 2, &ok);
    if (ok){
        declination = tmp;
        QMetaObject::invokeMethod(rootObject, "setDeclination",
                                    Q_ARG(QVariant, declination));
  }
}


void MainWindow::on_actionBattery_Forward_triggered(bool checked) {
    QMetaObject::invokeMethod(rootObject, "setBatteryForward",
                                    Q_ARG(QVariant, checked));

}

void MainWindow::on_actionUTC_Offset_triggered() {
    bool ok;
    int hours = QInputDialog::getInt(this,"Time Offset from UTC","Offset",utc_offset,-12,12,1,&ok);
    if (ok) {
        utc_offset = hours;
        dateTicker->setTimeZone(QTimeZone(3600*hours));
        if (hours < 0) {
            ui->plot->xAxis->setLabel(QString("Hour:Minute (UTC%1)\nMonth/Day/Year").arg(QString::number(hours)));
        } else if (hours == 0) {
            ui->plot->xAxis->setLabel(QString("Hour:Minute (UTC)\nMonth/Day/Year"));
        } else {
            ui->plot->xAxis->setLabel(QString("Hour:Minute (UTC+%1)\nMonth/Day/Year").arg(QString::number(hours)));
        }
        dateTicker->setTickOrigin(-3600*hours);
        ui->plot->replot();
    }
}

  void MainWindow::on_actionCalibration_Constants_triggered(){
    ConstantsDialog dialog;

    dialog.SetConstants(Vcal, Acal);
    dialog.exec();
  }