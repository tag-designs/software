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
#include "compass_processor.h"
#include "tickerdatetimeoffset.h"
#include "ui_mainwindow.h"
#include "constants_dialog.h"

// Track the plot cursor, display a compact tooltip, and update the QML compass
// with the nearest orientation sample. This is the only place where the plot
// position drives the compass widget.

void MainWindow::onMouseMove(QMouseEvent *event)
{
  QCustomPlot *customPlot = qobject_cast<QCustomPlot *>(sender());
  if (customPlot->axisRect()->rect().contains(event->pos())) {
    double x = ui->plot->xAxis->pixelToCoord(event->pos().x());
    double y = activityAxis->pixelToCoord(event->pos().y());
    double t = temperatureAxis->pixelToCoord(event->pos().y());
    double v = voltageAxis->pixelToCoord(event->pos().y());
    double h = headingAxis->pixelToCoord(event->pos().y());
    //double v = customPlot->yAxis3->pixelToCoord(event->pos().y());

    

    QDateTime dt = QDateTime::fromSecsSinceEpoch(qint64(x),QTimeZone(3600*(utc_offset)));//QTimeZone::utc());
    QString s;
    QTextStream out(&s);

    // Start with activity because it is the default left-axis stream.
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

    // Heading and orientation use the same time vector. The heading graph feeds
    // the tooltip while the derived sample feeds the QML compass display.
    index = headingGraph->findBegin(x);
    if (index) {
      h = headingGraph->dataMainValue(index);
      out << QString(", %1").arg(h,0,'f',1);
      CompassDerivedSample s = orientation[index];
      compassDisplay.showSample(s);
    }
    out << ")";

    QPoint globalPos = mapToGlobal(event->pos());
    QToolTip::showText(globalPos,out.readAll(),this, QRect(),3000);

    customPlot->replot(); //QCustomPlot::rpQueuedReplot);
  } 
}

// Legacy checkbox slot retained for the UI file. Menu actions are the main
// visibility controls, but this keeps older connections harmless.

void MainWindow::on_cb_activity_clicked(bool checked)
{
  activityGraph->setVisible(checked);
  activityAxis->setVisible(checked);
  ui->plot->replot();
}

// Redraw full graph

void MainWindow::on_pb_redraw_clicked()
{
  ui->plot->xAxis->setRange(activity_time[0], activity_time[activity_time.size() - 1]);
  ui->plot->replot();
}

// Pop up About window

void MainWindow::on_actionAbout_triggered()
{
  QMessageBox msgBox;
  msgBox.setText("Compass CompassVisualization Tool\nWritten by Geoffrey Brown\n2026");
  msgBox.exec();
}

void MainWindow::on_actionVoltage_triggered(bool checked)
{
    // Voltage and core temperature share the right-side lane, so enabling one
    // hides the other.
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
    // Temperature and voltage are mutually exclusive through vt_group; the
    // explicit graph visibility here keeps the axis state in sync.
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

void MainWindow::on_actionActivity_triggered(bool checked){
 // Activity and acceleration share the left-side lane.
 if (checked) {
        activityGraph->setVisible(true);
        activityAxis->setVisible(true);
        accelGraph->setVisible(false);
        accelAxis->setVisible(false);
    } else {
        activityGraph->setVisible(false);
        activityAxis->setVisible(false);
    }
    ui->plot->replot();

}

void MainWindow::on_actionAcceleration_triggered(bool checked){
 // Acceleration is derived from compass accelerometer magnitude and uses the
 // orientation time vector.
 if (checked) {
        accelGraph->setVisible(true);
        accelAxis->setVisible(true);
        activityGraph->setVisible(false);
        activityAxis->setVisible(false);
    } else {
        accelGraph->setVisible(false);
        accelAxis->setVisible(false);
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

  // Render the framed plot window rather than only the QCustomPlot so printed
  // output matches the visible Plot tab.
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
        compassDisplay.setDeclination(declination);
  }
  updateHeadingGraph();
}


void MainWindow::on_actionBattery_Forward_triggered(bool checked) {
    // Battery direction is a view convention. Rebuild the plotted heading and
    // tell QML how to draw the compass, but leave loaded samples unchanged.
    updateHeadingGraph();
    compassDisplay.setBatteryForward(checked);

}

void MainWindow::on_actionUTC_Offset_triggered() {
    bool ok;
    int hours = QInputDialog::getInt(this,"Time Offset from UTC","Offset",utc_offset,-12,12,1,&ok);
    if (ok) {
        utc_offset = hours;

        // Only the labels/ticks change. Stored timestamps remain Unix epoch
        // seconds in UTC.
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

    dialog.setConstants(calibration);
    dialog.exec();
  }

  void MainWindow::updateHeadingGraph(){
    qsizetype i;
    qsizetype len = orientation.length();
    bool forward = ui->actionBattery_Forward->isChecked();

    // Orientation samples are stored relative to magnetic north. Apply the
    // user-selected view transforms here so declination and battery direction
    // can be changed without reloading or recalibrating the log.
    for (i = 0; i < len; i++) {
        double h = CompassProcessor::headingFromYaw(orientation[i].yaw, declination);
        if (!forward) {
            h = CompassProcessor::headingFromYaw(h, 180.0);
        }
        heading[i] = h;
        //qInfo() << heading[i];
    }
    headingGraph->setData(orientation_time,heading,true);
    ui->plot->replot();
  }

  void MainWindow::on_actionReset_triggered(){
    // Rescale both axes to fit all data

    ui->plot->xAxis->rescale(true);
    ui->plot->xAxis->scaleRange(1.1, ui->plot->xAxis->range().center()); // 10% margin
    ui->plot->replot();
  }

  // context menus

  void MainWindow::showPlotContextMenu(QPoint pos){
    QMenu menu(this);
    // Mirror the main menus in the plot context menu so graph operations are
    // available near the data.
    menu.addAction(ui->actionReset);
    menu.addSeparator();
    menu.addAction(ui->actionActivity);
    menu.addAction(ui->actionAcceleration);
    menu.addSeparator();
    menu.addAction(ui->actionHeading);
 
    menu.addSeparator();
    menu.addAction(ui->actionVoltage);
    menu.addAction(ui->actionTemperature);
    menu.addSeparator();
    menu.addAction(ui->actionUTC_Offset);
    menu.addSeparator();
    menu.addAction(ui->actionReset);
    menu.addAction(ui->actionZoom_to_Cursors);
    menu.addSeparator();
    menu.addAction(ui->actionLoad);
    menu.addAction(ui->actionPrint);
    // Display the menu at the global position
    menu.exec(ui->plot->mapToGlobal(pos));

  }

  void MainWindow::showCompassContextMenu(QPoint pos) {
     QMenu menu(this);
    // Compass-specific context menu; plot stream controls belong on the plot.
    menu.addAction(ui->actionBattery_Forward);
    menu.addAction(ui->actionCompass_Declination);
    menu.addSeparator();
    menu.addAction(ui->actionCalibration_Constants);
    // Display the menu at the global position
    menu.exec(ui->quickWidget->mapToGlobal(pos));
  }

  void MainWindow::plot_doubleclick(QMouseEvent *event){
    QPoint point = event->pos();
    double x = ui->plot->xAxis->pixelToCoord(event->pos().x());
    double y = ui->plot->yAxis->pixelToCoord(event->pos().y());
    qDebug() << "Double Clicked at:" << x << y;
    if (left && right)
    {
      // Left-click places the left cursor. Shift-left-click places the right
      // cursor. Each cursor is constrained not to cross the other.
      if ((x <= right->start->coords().x()) &&
          (event->button() & Qt::LeftButton) &&
          !(event->modifiers() & Qt::ShiftModifier))
      {
        left->start->setCoords(x, QCPRange::minRange);
        left->end->setCoords(x, QCPRange::maxRange);
        //ui->te_left->setDateTime(
         //   QDateTime::fromSecsSinceEpoch(qint64(x), Qt::UTC));
         qDebug() << "move left cursor: " << x;
      }

      if ((x >= left->start->coords().x()) &&
          (event->button() & Qt::LeftButton) &&
          (event->modifiers() & Qt::ShiftModifier))
      {
        right->start->setCoords(x, QCPRange::minRange);
        right->end->setCoords(x, QCPRange::maxRange);
        qDebug() << "move right cursor: " << x;
        //ui->te_right->setDateTime(
            //QDateTime::fromSecsSinceEpoch(qint64(x), Qt::UTC));
      }
    }
    ui->plot->replot();
  }

  void MainWindow::on_actionZoom_to_Cursors_triggered(){
    if (left && right)
    {
      // The cursors define a time window only; y-axis ranges are unchanged.
      ui->plot->xAxis->setRange(left->start->coords().x(),
                                right->start->coords().x());
      ui->plot->replot();

    }
  }
