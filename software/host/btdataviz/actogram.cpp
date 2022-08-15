#include "actogram.h"
#include "solpos00.h"
#include "ui_actogram.h"
#include <FastFIR/FastFIR/qjfastfir.h>
#include <iostream>

static QBrush SunBrush = QBrush(QColor(255,255,0,50));   // yellow transparent
static QBrush BarBrush = QBrush(QColor(0, 0, 175, 100)); // light blue

Actogram::Actogram(QWidget *parent) : QWidget(parent), ui(new Ui::Actogram) {
  ui->setupUi(this);

  // Create the shared objects

  dateTicker = QSharedPointer<TickerDateTimeOffset>(new TickerDateTimeOffset);
  dateTicker->setDateTimeFormat("hh:mm");
  dateTicker->setDateTimeSpec(Qt::UTC);
  dateTicker->setTickCount(7);

  // Create a new plot layout when data is loaded

  ui->actoplot->clearPlottables();
  ui->actoplot->plotLayout()->clear();

  // Connect latitude/longitude spin boxes

  connect(ui->latitude, SIGNAL(valueChanged(double)), this,
          SLOT(location_changed()));
  connect(ui->latitude, SIGNAL(editingFinished()), this,
          SLOT(location_changed()));
  connect(ui->longitude, SIGNAL(valueChanged(double)), this,
          SLOT(location_changed()));
  connect(ui->longitude, SIGNAL(editingFinished()), this,
          SLOT(location_changed()));
}

Actogram::~Actogram() { delete ui; }

// Aggregate new data

void Actogram::setData(QVector<double> time, QVector<double> value,
                       QString fname) {
  tmdata.clear();
  valdata.clear();
  fileName = fname;
  ui->le_actogram->setText((fileName));

  // build aggregated data arrays

  if (time.length() == 0)
        return;

  tmdata = time;
  valdata = value;

  // initialize the date ranges

  startDT = QDateTime::fromSecsSinceEpoch(qint64(tmdata[0]), Qt::UTC);
  endDT = QDateTime::fromSecsSinceEpoch(qint64(tmdata.last()), Qt::UTC);
  ui->startDay->setMinimumDateTime(startDT);
  ui->startDay->setMaximumDateTime(endDT);
  ui->sb_startday->setMaximum(startDT.daysTo(endDT));
  ui->sb_startday->setValue(0);

  // plot everything

  generateNaturalLightData();
  drawActogram();
}

// Build the actogram array

void Actogram::drawActogram() {

  // dump existing layout

  actos.clear();
  rects.clear();

  for (int i = 0; i < labels.size(); i++)
      ui->actoplot->removeItem(labels[i]);
  labels.clear();
  ui->actoplot->clearPlottables();
  ui->actoplot->plotLayout()->clear();

  // build the plot

  int num_graphs = ui->days->value();

  // generate the graphs

  for (int i = 0; i < num_graphs; i++) {
    QCPAxisRect *rect = new QCPAxisRect(ui->actoplot);
    //QCPTextElement *label = new QCPTextElement(ui->actoplot);

    ui->actoplot->plotLayout()->addElement(i, 0, rect);
   // ui->actoplot->plotLayout()->addElement(i, 0, label);

    foreach (QCPAxis *axis, rect->axes()) {
      axis->setLayer("axes");
      axis->grid()->setLayer("grid");
    }

    rects << rect;

    // Create Axes

    QCPAxis *xAxis = rect->axis(QCPAxis::atBottom);
    QCPAxis *yAxis = rect->axis(QCPAxis::atLeft);
    QCPAxis *yyAxis = rect->axis(QCPAxis::atRight);


    // Xaxis is time with custom ticker

    xAxis->setTicker(dateTicker);
    if (num_graphs > 10){
        xAxis->setTickLabels(false);
    }

    if (num_graphs > 10) {
     rect->setMinimumMargins(QMargins(70, 0, 0, 0));
    } else {
     rect->setMinimumMargins(QMargins(70, 0, 0, 25));
    }
    // Yaxis is activity with adjustible range -- 0..100%

    yAxis->setTickLabels(false);
    yAxis->setRange(0, ui->sb_actorange->value());

    // Light graph axis 0..90 for sun elevation

    yyAxis->setTicks(false);
    yyAxis->setTickLabels(false);
    yyAxis->setRange(0, 90.0);

    // create a bargraph


    QCPGraph *acto = new QCPGraph(xAxis, yAxis);
    //acto->setWidthType(QCPBars::wtPlotCoords);
    //acto->setWidth(double(agg_time));
    acto->setPen(Qt::NoPen);
    acto->setBrush(BarBrush);

    acto->setData(tmdata, valdata, true);
    actos << acto;

    // Add natural lighting  (order puts it behind graph

    if (ui->rb_natural_light->isChecked() && lightdata.size()) {   
      QCPGraph *lightgraph = new QCPGraph(xAxis, yyAxis);
      lightgraph->setPen(Qt::NoPen);
      lightgraph->setBrush(SunBrush);
      lightgraph->setData(tmdata, lightdata, true);
    }

    if (ui->rb_lab_light->isChecked() && tmLabLight.size()) {
        QCPGraph *lightgraph = new QCPGraph(xAxis, yyAxis);
        lightgraph->setPen(Qt::NoPen);
        lightgraph->setBrush(SunBrush);
        lightgraph->setData(tmLabLight, valLabLight, true);
    }

    // label experiment

    QCPItemText *subFigLabel = new QCPItemText(ui->actoplot);
    subFigLabel->setClipToAxisRect(false);
    subFigLabel->position->setAxisRect(rect);
    subFigLabel->position->setType(QCPItemPosition::ptAxisRectRatio);
    subFigLabel->position->setCoords(0, 0.5); // center left of axis rect...
    subFigLabel->setPositionAlignment(Qt::AlignVCenter|Qt::AlignRight); // ...matches center right of label
    //subFigLabel->setPadding(QMargins(0, 0, 0, 0)); // and add some padding on right of label

    labels << subFigLabel;
  }
  //ui->actoplot->plotLayout()->setColumnStretchFactor(0,0.1);
 // ui->actoplot->plotLayout()->setColumnStretchFactor(1,2.0);


  drawMetadata(num_graphs);
  on_startDay_dateChanged(ui->startDay->date());
}

// Draw metadata on plot

void Actogram::drawMetadata(int num_graphs) {

  // Add footer

  QCPLayoutGrid *subLayout = new QCPLayoutGrid;
  ui->actoplot->plotLayout()->insertRow(num_graphs);
  ui->actoplot->plotLayout()->addElement(num_graphs, 0, subLayout);

  footleft = new QCPTextElement(ui->actoplot);
  footcenter = new QCPTextElement(ui->actoplot);
  footright = new QCPTextElement(ui->actoplot);

  footleft->setFont(QFont("sans", 12));
  footleft->setTextFlags(Qt::AlignLeft);

  footcenter->setFont(QFont("sans", 12, QFont::Bold));


  footright->setFont(QFont("sans", 12));
  footright->setTextFlags(Qt::AlignRight);

  footleft->setText(QString("Activity range: ") +
                    ui->sb_actorange->text() +
                    QString("%"));

  footright->setText(QString("File: ") + fileName);

  subLayout->addElement(0, 0, footleft);
  subLayout->addElement(0, 1, footcenter);
  subLayout->addElement(0, 2, footright);

  // Add a title (possibly empty)

  title = new QCPTextElement(ui->actoplot);
  title->setFont(QFont("sans", 12, QFont::Bold));
  title->setText(fileName);

  if (num_graphs > 10) {  // optimize layout
   footcenter->setMinimumSize(100,100);
   title->setMinimumSize(100,100);
  }

  ui->actoplot->plotLayout()->insertRow(0);
  ui->actoplot->plotLayout()->addElement(0, 0, title);
}

// This is the main refresh code

void Actogram::on_startDay_dateChanged(const QDate &date) {
  if (rects.size()) {
    int utcoffset = ui->sb_offset->value();
    if (utcoffset > 0)
      footcenter->setText(QString("UTC+") + QString::number(utcoffset));
    else if (utcoffset < 0)
      footcenter->setText(QString("UTC") + QString::number(utcoffset));
    else
      footcenter->setText(QString("UTC"));

    int offset = utcoffset * 3600;

    dateTicker->setOffset(offset);

    // a bit of a mess to get utc from date !
    // qint64 start = QDateTime(date, QTime(),
    // Qt::OffsetFromUTC,offset).toSecsSinceEpoch();

    qint64 start = QDateTime(date, QTime(), Qt::UTC).toSecsSinceEpoch()
                   - offset;

    // update ticker for (possibly) new UTC offset

    dateTicker->setTickOrigin(double(start));

    // set time range of the plots

    int width = ui->cb_double->isChecked() ? 3600 * 48 : 3600 * 24;

    for (int i = 0; i < rects.size(); i++) {
      QCPAxis *xAxis = rects[i]->axis(QCPAxis::atBottom);
     // QCPAxis *yAxis = rects[i]->axis(QCPAxis::atLeft);

      xAxis->setRange(start, start + width);
    labels[i]->setText(
   // yAxis->setLabel(
          QDateTime::fromSecsSinceEpoch(start, Qt::OffsetFromUTC, offset)
              .toString("M/d  "));
      start += 3600 * 24;
    }

    // update the scroll start date bar

    ui->sb_startday->setValue(
        ui->startDay->minimumDate().daysTo(ui->startDay->date()));

    // replot everything

    ui->actoplot->replot();
  }
}

//
// Control Main Graph
//

// Set Y Range

void Actogram::on_sb_actorange_valueChanged(const QString &arg1) {
  for (int i = 0; i < rects.size(); i++) {
    rects[i]->axis(QCPAxis::atLeft)->setRange(0, ui->sb_actorange->value());
  }
  if (rects.size()) {
    QString ls =
        QString("Activity range: ") + ui->sb_actorange->text() + QString("%");
    footleft->setText(ls);
    ui->actoplot->replot();
  }
}

// Move start date

void Actogram::on_sb_startday_valueChanged(int value) {
  ui->startDay->setDate(ui->startDay->minimumDate().addDays(value));
}

void Actogram::on_days_valueChanged(const QString &arg1) { drawActogram(); }

// Handle changes to title

void Actogram::on_le_actogram_textChanged(const QString &arg1) {
  if (rects.size()) {
    title->setText(arg1);
    ui->actoplot->replot();
  }
}

// Handle UTC offset change

void Actogram::on_sb_offset_valueChanged(int arg1) {
  //dateTicker->offset = arg1 * 3600.0;
  on_startDay_dateChanged(ui->startDay->date());
}

// Plot double/single

void Actogram::on_cb_double_toggled(bool checked) {
  on_startDay_dateChanged(ui->startDay->date());
  ui->actoplot->replot();
}

//
// Export plots
//

void Actogram::on_pb_acto_png_clicked() {
  QString fileName = QFileDialog::getSaveFileName(
      this, tr("Save File"), QDir::homePath() + "/untitled.png",
      tr("Actogram (*.png)"));

  ui->actoplot->savePng(fileName, 0, 0, 1.0, -1, 300);
}

void Actogram::on_pb_acto_pdf_clicked() {
  QString fileName = QFileDialog::getSaveFileName(
      this, tr("Save File"), QDir::homePath() + "/untitled.pdf",
      tr("Protobuf (*.pdf)"));

  ui->actoplot->savePdf(fileName);
}

//
// Control daylight graph
//

void Actogram::on_rb_no_light_clicked() {
  drawActogram();
}

void Actogram::on_rb_natural_light_clicked() {
  drawActogram();
}

void Actogram::on_rb_lab_light_clicked()
{
  drawActogram();
}


// Location change

void Actogram::location_changed() {
  generateNaturalLightData();
  if (ui->rb_natural_light->isChecked())
     drawActogram();
}

// Generate Natural Daylight data

void Actogram::generateNaturalLightData() {
  struct posdata posData;
  long err;
  lightdata.clear();

  S_init(&posData);
  posData.latitude = ui->latitude->value();
  posData.longitude = ui->longitude->value();
  posData.function = S_ETR;

  // generate some test data

  for (int i = 0; i < tmdata.size(); i++) {
    QDateTime dt = QDateTime::fromSecsSinceEpoch(qint64(tmdata[i]), Qt::UTC);
    posData.daynum = dt.date().dayOfYear();
    posData.year = dt.date().year();
    posData.hour = dt.time().hour();
    posData.minute = dt.time().minute();
    posData.second = dt.time().second();
    posData.timezone = 0.0;

    err = S_solpos(&posData);
    if (err) {
      fprintf(stderr, "error %ld\n", err);
      lightdata.clear();
      break;
    }
    // add civil twilight angle (6 degrees)
    lightdata << posData.elevref + 6.0;
  }
}


void Actogram::on_pb_load_lightfile_clicked()
{
    QMessageBox msgBox;
    const QString format = "MM/dd/yyyy hh:mm:ss";
    int i;

    QString inFileName = QFileDialog::getOpenFileName(this, tr("Open Activity CSV Specification"), QDir::homePath(),
                                                    tr("Light Files (*.csv)"));

    if (inFileName == 0)
        return;

    QFile infile(inFileName);
    if (!infile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      msgBox.setText("couldn't open:" + inFileName);
      msgBox.exec();
      return;
    }

    LabLightFile = inFileName;
    tmLabLight.clear();
    valLabLight.clear();

    double last = 0;

    while (!infile.atEnd()) {
      QString line = infile.readLine();
      QStringList l1 = line.trimmed().split(',');
      if (l1.size() < 2) {
          msgBox.setText("couldn't parse input format: start,stop --" + line);
          msgBox.exec();
          tmLabLight.clear();
          valLabLight.clear();
          break;
      }

      QDateTime start = QDateTime::fromString(l1[0],format);
      QDateTime stop  = QDateTime::fromString(l1[1],format);
      if (stop.isNull() || start.isNull()) {
           msgBox.setText("couldn't parse start,stop: " + line + " format: " + format);
           msgBox.exec();
           tmLabLight.clear();
           valLabLight.clear();
           break;
      }

      start.setTimeSpec(Qt::UTC);
      stop.setTimeSpec(Qt::UTC);
      double start_time = double(start.toSecsSinceEpoch());
      double stop_time = double(stop.toSecsSinceEpoch());

      if ((start_time <= last) || (stop_time <= start_time)) {
          msgBox.setText("all times must be ordered and not equal");
          msgBox.exec();
          tmLabLight.clear();
          valLabLight.clear();
          break;
      }
      tmLabLight << start_time;
      valLabLight << 0.0;
      tmLabLight << start_time+1;
      valLabLight << 45;
      tmLabLight << stop_time-1;
      valLabLight << 45;
      tmLabLight << stop_time;
      valLabLight << 0.0;
      last = stop_time;
     }

    if (tmLabLight.size())
         ui->rb_lab_light->setEnabled(true);
    else
         ui->rb_lab_light->setEnabled(false);
    infile.close();

}



