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
  
  ui->plot->yAxis->setRange(0, ui->activityRange->value());
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

  // connect slots

  connect(ui->plot, SIGNAL(mouseMove(QMouseEvent *)),
          SLOT(onMouseMove(QMouseEvent *)));
  connect(ui->plot, SIGNAL(mouseDoubleClick(QMouseEvent *)),
          SLOT(plot_doubleclick(QMouseEvent *)));
  connect(ui->plot->xAxis, SIGNAL(rangeChanged(QCPRange)),
          SLOT(activityLevel(QCPRange)));
}

MainWindow::~MainWindow() { delete ui; }

// Enable on/off

void MainWindow::makeVisible(bool visible)
{
  ui->pb_redraw->setEnabled(visible);
  ui->pb_print->setEnabled(visible);
  ui->gb_graph->setEnabled(visible);
  ui->gb_cursors->setEnabled(visible);
  ui->gb_filterparams->setEnabled(visible);
  ui->gb_activityfilter->setEnabled(visible);
  ui->gb_export->setEnabled(visible);
  ui->gb_timeoffset->setEnabled(visible);
  // ui->pb_process->setEnabled(visible);
  ui->theActogram->setEnabled(visible);
  //  ui->gb_display->setEnabled(visible);
}

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
  activityLevel(ui->plot->xAxis->range());
}

// Pop up About window

void MainWindow::on_actionAbout_triggered()
{
  QMessageBox msgBox;
  msgBox.setText("BitTag Visualization Tool\nWritten by Geoffrey Brown\n2019");
  msgBox.exec();
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
  accel_count.clear();
  ui->te_fileinfo->clear();
  ui->te_fileinfo->appendPlainText(fileName);
  ui->plot->graph(0)->data()->clear();
  ui->plot->graph(1)->data()->clear();
  ui->plot->graph(2)->data()->clear();
  tagtype = BITTAG;

  // parse the file 
  while (!in.atEnd())
  {
    QString line = in.readLine();
    if (line[0] == '#')
    {
      if (!line.startsWith("# Page"))
        ui->te_fileinfo->appendPlainText(line);
      if (line.contains(QString("PRESTAG"))){
        tagtype = PRESTAG;
        //ui->te_fileinfo->appendPlainText("saw PRESTAG");
      }
       if (line.contains(QString("BITTAGNG"))){
        tagtype = BITTAGNG;
        //ui->te_fileinfo->appendPlainText("saw PRESTAG");
      }
    }
    else
    {
      // parse non-comment strings
      // currently assumes the old data format
      // and only works for minute buckets

      QStringList l1 = line.split(',');

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
        case PRESTAG:
        if (l1.length() >= 2)
          {
              if (l1.length() > 2 && (l1[1][0] == 'V'))
              {
                QStringList v = l1[1].split(':');
                //QStringList t = l1[2].split(':');
                voltage_time << l1[0].toInt();
                //temperature_time << l1[0].toInt();
                //temperature << t[1].toDouble();
                voltage << v[1].toDouble();
              }
              else
              {
                QStringList dat = l1[1].split(':');
                double time = l1[0].toInt();
                if (dat[0].startsWith(QString("P")))
                {
                    accel_time << time;
                    accel_count << dat[1].toDouble();
                }
                if (dat[0].startsWith(QString("T")))
                {
                    temperature_time << time;
                    temperature << dat[1].toDouble();
                 
                }
              }
          }
        break;
        case BITTAGNG:
        if (l1.length() >= 2)
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
                if (dat[0].startsWith(QString("A")))
                {
                    accel_time << time;
                    accel_count << dat[1].toDouble();
                }
              }
          }
        break;
      }

      // Customize UI for the tag types

      // Set range to 500 hPA

      if (tagtype == PRESTAG) {
        ui->activityRange->setMaximum(1050);
        ui->activityRange->setValue(1050);
        ui->activityRange->setMinimum(600);
        ui->activityRange->setEnabled(false);
        ui->activityRange->setVisible(false);
        ui->label->setVisible(false);
        ui->cb_activity->setText("Pressure");
        ui->tabConfig->setTabEnabled(1,false);
        ui->plot->yAxis->setLabel("Pressure (hPa)");
        ui->plot->yAxis->setRange(400, ui->activityRange->value());
      }
      if (tagtype == BITTAG || tagtype == BITTAGNG) {
        ui->activityRange->setMaximum(105);
        ui->activityRange->setMaximum(100);
        ui->activityRange->setEnabled(true);
        ui->activityRange->setVisible(true);
        ui->label->setVisible(true);
        ui->cb_activity->setText("Activity");
        ui->tabConfig->setTabEnabled(1,true);
       ui->plot->yAxis->setLabel("Activity Percent");
      }
    }
  }

  file.close();

  // reset cursors and range

  if (accel_time.size())
  {
    // reset cursors
    double size = accel_time.size();

    // left
    ui->te_left->setMinimumDateTime(
        QDateTime::fromSecsSinceEpoch(qint64(accel_time[0]), Qt::UTC));
    ui->te_left->setDateTime(
        QDateTime::fromSecsSinceEpoch(qint64(accel_time[0]), Qt::UTC));
    left->setVisible(true);

    // right
    ui->te_right->setMaximumDateTime(
        QDateTime::fromSecsSinceEpoch(qint64(accel_time[size - 1]), Qt::UTC));
    ui->te_right->setDateTime(
        QDateTime::fromSecsSinceEpoch(qint64(accel_time[size - 1]), Qt::UTC));
    right->setVisible(true);

    // set range

    ui->plot->xAxis->setRange(accel_time[0], accel_time[size - 1]);
  }

  // plot activity data -- this will trigger graph redraw

  ui->theActogram->setData(accel_time, accel_count,
                           QFileInfo(currentfilename).fileName());

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
 * Activity graph -- apply filter
 */

void MainWindow::activityLevel(QCPRange range)
{
  double x_axis_range_lower = range.lower;
  double x_axis_range_upper = range.upper;
  int count = 0;
  double sum = 0.0;

  QCPGraphDataContainer::const_iterator begin =
      ui->plot->graph(0)->data()->constBegin();
  QCPGraphDataContainer::const_iterator end =
      ui->plot->graph(0)->data()->constEnd();

  while (begin != end)
  {
    if (begin->key > x_axis_range_upper)
      break;
    if (begin->key > x_axis_range_lower)
    {
      sum += begin->value;
      count++;
    }
    ++begin;
  }
  if (count)
  {
    ui->Activitylevel->setText(QString::number((sum / count), 'f', 2));
  }
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

void MainWindow::on_sb_cutoff_valueChanged(double freq)
{
  const int filter_delay = 50;
  if (accel_count.size() > filter_delay)
  {

    // warning -- switched to slow fir because fast fir 
    // introduced unpredictable delay
    
    QVector<double> filterData(accel_count);
    QJSlowFIRFilter *fastfir;

    //create FastFIR filter
    fastfir = new QJSlowFIRFilter(this);
    fastfir->setKernel(QJFilterDesign::LowPassHanning(ui->sb_cutoff->value(), 1.0, 2*filter_delay+1));
    fastfir->Update(filterData);
    delete fastfir;

    accel_time_filtered.resize(filterData.size() - filter_delay);
    accel_count_filtered.resize(filterData.size() - filter_delay);

    for (int i = 0; i < accel_time_filtered.size(); i++) {
      accel_time_filtered[i] = accel_time[i];
      accel_count_filtered[i] = filterData[i+filter_delay];
    }
    on_cb_filter_low_pass_toggled(ui->cb_filter_low_pass->isChecked());
  }
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
        accelData[i].value = accel_count_filtered[i];
        accelData[i].key = accel_time_filtered[i];
      }
      //ui->plot->yAxis->setRange(0, 25);
      ui->plot->graph(0)->data()->set(accelData);
      ui->theActogram->setData(accel_time_filtered, accel_count_filtered,
                               QFileInfo(currentfilename).fileName());
  }
  else
  {
    QVector<QCPGraphData> accelData(accel_time.size());
    for (int i = 0; i < accel_time.size(); i++)
    {
      accelData[i].value = accel_count[i];
      accelData[i].key = accel_time[i];
    }
    ui->plot->graph(0)->data()->set(accelData);
    if (tagtype == PRESTAG)
      ui->plot->yAxis->setRange(500, ui->activityRange->value());
    else
      ui->plot->yAxis->setRange(0, ui->activityRange->value());
    ui->theActogram->setData(accel_time, accel_count,
                             QFileInfo(currentfilename).fileName());
  }
  ui->plot->replot();
  activityLevel(ui->plot->xAxis->range());
}

/*
 * Export
 */

void MainWindow::on_pb_pdf_clicked()
{
  QString fileName = QFileDialog::getSaveFileName(
      this, tr("Save File"), QDir::homePath() + "/untitled.pdf",
      tr("Protobuf (*.pdf)"));

  ui->plot->savePdf(fileName);
}

void MainWindow::on_pb_png_clicked()
{
  QString fileName = QFileDialog::getSaveFileName(
      this, tr("Save File"), QDir::homePath() + "/untitled.png",
      tr("Protobuf (*.png)"));

  ui->plot->savePng(fileName, 0, 0, 1.0, -1, 300);
}

void MainWindow::on_pb_export_csv_clicked()
{
  QMessageBox msgBox;
  int hours = ui->offsetUTC->value();
  QTimeZone timezone = QTimeZone(3600*hours);
  QString fileName = QFileDialog::getSaveFileName(
      this, tr("Save File"), QDir::homePath() + "/untitled.csv",
      tr("CSV (*.csv)"));
  QFile file(fileName);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
  {
    msgBox.setText("couldn't open:" + fileName);
    msgBox.exec();
    return;
  }
  QTextStream out(&file);

  if (ui->cb_filter_low_pass->isChecked())
  {
    out << "# low pass filter cutoff frequency = " << ui->sb_cutoff->value() <<"\n";
  }
  else
  {
    out << "# raw data\n";
  }

  switch (tagtype)
  {
  case BITTAG:
  case BITTAGNG:

    /* code */
    out << QString("timestamp start,time start (offset hours: %1),activity percentage,temperature,voltage\n").arg(QString::number(hours));
    break;
  case PRESTAG:
    out << QString("timestamp start,time start (offset hours: %1),pressure (hPa),temperature,voltage\n").arg(QString::number(hours));
    break;
  default:
    break;
  }
  
  
  double x_axis_range_lower = ui->plot->xAxis->range().lower;
  double x_axis_range_upper = ui->plot->xAxis->range().upper;
  const QString format = "MM/dd/yyyy hh:mm:ss";

  // dump activity/pressure data

  QCPGraphDataContainer::const_iterator activity_begin = ui->plot->graph(0)->data()->findBegin(x_axis_range_lower);
  QCPGraphDataContainer::const_iterator activity_end = ui->plot->graph(0)->data()->findEnd(x_axis_range_upper);
  QCPGraphDataContainer::const_iterator temperature_begin = ui->plot->graph(1)->data()->findBegin(x_axis_range_lower);
  QCPGraphDataContainer::const_iterator temperature_end = ui->plot->graph(1)->data()->findEnd(x_axis_range_upper);
  QCPGraphDataContainer::const_iterator voltage_begin = ui->plot->graph(2)->data()->findBegin(x_axis_range_lower);
  QCPGraphDataContainer::const_iterator voltage_end = ui->plot->graph(2)->data()->findEnd(x_axis_range_upper);

  int32_t last_min = INT32_MAX;

  while ((activity_begin != activity_end) || 
         (temperature_begin != temperature_end) ||
         (voltage_begin != voltage_end))
         {
             int32_t min =  INT32_MAX;
             int32_t activity_key = (activity_begin != activity_end)? activity_begin->key :  INT32_MAX;
             int32_t temperature_key = (temperature_begin != temperature_end)? temperature_begin->key :  INT32_MAX;
             int32_t voltage_key = (voltage_begin != voltage_end)? voltage_begin->key : INT32_MAX;
             min = activity_key < min ? activity_key : min;
             min = temperature_key < min ? temperature_key : min;
             min = voltage_key < min ? voltage_key : min;

             if (min ==  INT32_MAX)
                break;

             if (activity_key == last_min) {
                ui->te_fileinfo->appendPlainText("Error:  Repeated activity data time!");
             } 
             
             last_min = min;

             out << min << "," << QDateTime::fromSecsSinceEpoch(qint64(min), timezone).toString(format) << ",";

             if (min == activity_key) {
               out << QString::number(activity_begin->value, 'f', 2);
               ++activity_begin;
             }
             out << ",";
             if (min == temperature_key) {
               out << QString::number(temperature_begin->value, 'f', 2);
               ++temperature_begin;
             }
             out << ",";
             if (min == voltage_key) {
               out << QString::number(voltage_begin->value, 'f', 2);
               ++voltage_begin;
             }
             out << "\n";

         }
  file.close();
}

// Print current graph

void MainWindow::on_pb_print_clicked()
{
  QPrinter printer;
  QPrintPreviewDialog previewDialog(&printer, this);
  connect(&previewDialog, SIGNAL(paintRequested(QPrinter *)),
          SLOT(renderPlot(QPrinter *)));
  previewDialog.exec();
}

void MainWindow::renderPlot(QPrinter *printer)
{
  printer->setPageSize(QPageSize(QPageSize::Letter));
  QCPPainter painter(printer);
  QRectF pageRect = printer->pageRect(QPrinter::DevicePixel);

  int plotWidth = ui->plot->viewport().width();
  int plotHeight = ui->plot->viewport().height();
  double scale = pageRect.width() / (double)plotWidth;

  painter.setMode(QCPPainter::pmVectorized);
  painter.setMode(QCPPainter::pmNoCaching);
  painter.setMode(
      QCPPainter::pmNonCosmetic); // comment this out if you want cosmetic
                                  // thin lines (always 1 pixel thick
                                  // independent of pdf zoom level)

  painter.scale(scale, scale);
  ui->plot->toPainter(&painter, plotWidth, plotHeight);
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
      ui->te_left->setDateTime(
          QDateTime::fromSecsSinceEpoch(qint64(x), Qt::UTC));
    }

    if ((x > left->start->coords().x()) &&
        (event->button() & Qt::RightButton))
    {
      ui->te_right->setDateTime(
          QDateTime::fromSecsSinceEpoch(qint64(x), Qt::UTC));
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
    activityLevel(ui->plot->xAxis->range());
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

void MainWindow::on_te_left_dateTimeChanged(const QDateTime &dateTime)
{
  double x = dateTime.toSecsSinceEpoch();

  ui->te_right->setMinimumDateTime(dateTime);
  left->start->setCoords(x, QCPRange::minRange);
  left->end->setCoords(x, QCPRange::maxRange);
  ui->plot->replot();
}

void MainWindow::on_te_right_dateTimeChanged(const QDateTime &dateTime)
{
  double x = dateTime.toSecsSinceEpoch();
  ui->te_left->setMaximumDateTime(dateTime);
  right->start->setCoords(x, QCPRange::minRange);
  right->end->setCoords(x, QCPRange::maxRange);
  ui->plot->replot();
}

void MainWindow::on_pb_process_clicked()
{
  QMessageBox msgBox;
  const QString format = "MM/dd/yyyy hh:mm:ss";

  QString inFileName =
      QFileDialog::getOpenFileName(this, tr("Open Activity CSV Specification"),
                                   path, tr("Data Files (*.csv)"));

  if (inFileName.isNull())
    return;
  QFile infile(inFileName);
  if (!infile.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    msgBox.setText("couldn't open:" + inFileName);
    msgBox.exec();
    return;
  }

  QString outFileName = QFileDialog::getSaveFileName(
      this, tr("Save File"), QDir::homePath() + "/untitled.csv",
      tr("Protobuf (*.csv)"));
  QFile outfile(outFileName);
  if (!outfile.open(QIODevice::WriteOnly | QIODevice::Text))
  {
    msgBox.setText("couldn't open:" + outFileName);
    msgBox.exec();
    infile.close();
    return;
  }
  QTextStream out(&outfile);
  out << "start,stop,activity percent," << currentfilename << "\n";

  while (!infile.atEnd())
  {
    QString line = infile.readLine();
    if (!line.startsWith("#"))
    {
      QStringList l1 = line.trimmed().split(',');
      QDateTime start = QDateTime::fromString(l1[0], format);
      if (start.isNull())
      {
        msgBox.setText("couldn't parse start: " + l1[0] + " format: " + format);
        msgBox.exec();
        break;
      }
      start.setTimeSpec((Qt::UTC));

      QDateTime stop = QDateTime::fromString(l1[1], format);
      if (stop.isNull())
      {
        msgBox.setText("couldn't parse stop: " + l1[1] + " format: " + format);
        msgBox.exec();
        break;
      }
      stop.setTimeSpec(Qt::UTC);
      qint64 start_time = start.toSecsSinceEpoch();
      qint64 stop_time = stop.toSecsSinceEpoch();
      int count = 0;
      double avg = 0.0;
      for (int i = 0; i < accel_time.length() && accel_time[i] < stop_time;
           i++)
      {
        if (accel_time[i] < start_time)
          continue;
        count++;
        avg += accel_count[i];
      }
      if (count)
      {
        out << start.toString(format) << ",";
        out << stop.toString(format) << ","
            << QString::number(avg / count, 'f', 2) << "\n";
      }
    }
  }

  infile.close();
  outfile.close();
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

void MainWindow::on_activityRange_valueChanged(int i) 
{
  if (tagtype == PRESTAG)
    ui->plot->yAxis->setRange(700, i);
  else
    ui->plot->yAxis->setRange(0, ui->activityRange->value());
  ui->plot->replot();
 //std::cerr << "set range\n";
}
