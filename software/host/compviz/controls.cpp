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

// Track mouse in plot and display time of location

void MainWindow::onMouseMove(QMouseEvent *event)
{
  QCustomPlot *customPlot = qobject_cast<QCustomPlot *>(sender());
  double x = customPlot->xAxis->pixelToCoord(event->pos().x());
  double y = customPlot->yAxis->pixelToCoord(event->pos().y());
  double t = customPlot->yAxis2->pixelToCoord(event->pos().y());
  //double v = customPlot->yAxis3->pixelToCoord(event->pos().y());

  int index = ui->plot->graph(0)->findBegin(x);
  if (index) {
    y = ui->plot->graph(0)->dataMainValue(index);
  }

  QDateTime dt = QDateTime::fromSecsSinceEpoch(qint64(x),QTimeZone(3600*(ui->offsetUTC->value())));//QTimeZone::utc());
  QString s;
  
  QTextStream out(&s);
  out << "(" << dt.toString("MM/dd hh:mm");
  out << QString(", %1").arg(y,0,'f',1);
  if (ui->gbVT->isChecked()) {
    int gi = ui->rbTemperature->isChecked() ? 1 : 2;
    int index = ui->plot->graph(gi)->findBegin(x);
    if (index) {
        t = ui->plot->graph(gi)->dataMainValue(index);
    }
    out << QString(", %1").arg(t,0,'f',1);
    out << (ui->rbTemperature->isChecked() ? "C" : "V");
  }
  out << ")";
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
  // refresh
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


void MainWindow::on_rbTemperature_clicked()
{
  ui->plot->yAxis2->setRange(0, 50);
  ui->plot->yAxis2->setLabel("Temperature C");
  ui->plot->yAxis2->setLabelColor(Qt::red);
  on_gbVT_clicked();
}

void MainWindow::on_rbVoltage_clicked()
{
  ui->plot->yAxis2->setRange(1.5, 3.5);
  ui->plot->yAxis2->setLabel("Voltage");
  ui->plot->yAxis2->setLabelColor(Qt::darkGreen);
  on_gbVT_clicked();
}


void MainWindow::on_gbVT_clicked()
{
  if (ui->gbVT->isChecked())
  {
    if (ui->rbTemperature->isChecked())
    {
      ui->plot->graph(1)->setVisible(true);
      ui->plot->graph(2)->setVisible(false);
    }
    else
    {
      ui->plot->graph(1)->setVisible(false);
      ui->plot->graph(2)->setVisible(true);
    }
    ui->plot->yAxis2->setVisible(true);
  }
  else
  {
    ui->plot->graph(2)->setVisible(false);
    ui->plot->graph(1)->setVisible(false);
    ui->plot->yAxis2->setVisible(false);
  }
  ui->plot->replot();
}

// plot data

void MainWindow::on_offsetUTC_valueChanged(int hours)
{
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
