
#include <QMainWindow>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTime>
#include <QDateTime>
#include <QTimer>
#include <QLayout>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <QProcessEnvironment>

#include <QVector3D>


#include <QFuture>

#include <ctime>
#include <fstream>
#include <iomanip>

#include <google/protobuf/util/json_util.h>
#include <streambuf>

#include <QRandomGenerator>


#include "tagclass.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "tag.pb.h"

#include "taglogs.h"


extern "C"
{
#include "log.h"
}

// hook into the error logging system

extern void myMessageOutput(QtMsgType type, const QMessageLogContext &context,
                            const QString &msg);
extern int log_level;
QTextEdit *s_textEdit = nullptr;

#define title_string "Tag Calibrator v0.1"


// main window

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  ui.setupUi(this);
  this->setAttribute(Qt::WA_AlwaysShowToolTips, true);

  // Change Main Window title

  setWindowTitle(QString(title_string));

  logWindowInit();

  generator = QRandomGenerator::global();

  // Connect Timers to slots

  connect(&timer, SIGNAL(timeout()), this, SLOT(TriggerUpdate()));
  QObject::connect(&magnetic, &CompassData::calibration_update, this, &MainWindow::calibration_update);
  connect(&qualitytimer, SIGNAL(timeout()), this, SLOT(TriggerQualityUpdate()));

  // attach to tag if possible

  Attach();
  ui.hi_graphicsView->redraw();
  ui.tagWidget->setSource(QUrl("qrc:/qfi/images/main.qml"));
  qInfo() << ui.tagWidget->errors();
  ui.tagWidget->show();
  qInfo() << ui.tagWidget->errors();
  rootObject = ui.tagWidget->rootObject();

  
  
}

MainWindow::~MainWindow()
{
  tag.Detach();
}

// attach to tag

bool MainWindow::Attach()
{
  if (tag.IsAttached())
  {
    qInfo() << "Already attached to tag";
    return false;
  }

  // Find base

  std::vector<UsbDev> usbdevs;

  if (!tag.Available(usbdevs) || (usbdevs.size() == 0))
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Warning");
    msgBox.setText("No Tag Bases Found");
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();
    qInfo() << "No Tag Bases Found";
    return false;
    //QTimer::singleShot(0, this, SLOT(close()));;
  }

  int index = 0;
  if (usbdevs.size() > 1)
  {
    QStringList items;

    for (int j = 0; j < usbdevs.size(); j++)
    {
      QString bus = QString::number(usbdevs[j].bus).rightJustified(3, '0');
      QString address = QString::number(usbdevs[j].address).rightJustified(3, '0');
      QString vid = QString::number(usbdevs[j].vid, 16); //.rightJustified(4,'0');
      QString pid = QString::number(usbdevs[j].pid, 16); //.rightJustified(4,'0');
      QString s = QString("%1:%2    0x%3:0x%4").arg(bus, address, vid, pid);
      items << s;
    }

    QInputDialog *inputDialog = new QInputDialog();
    inputDialog->setOption(QInputDialog::UseListViewForComboBoxItems);
    inputDialog->setComboBoxItems(items);
    inputDialog->setWindowTitle("Available Bases");
    inputDialog->setLabelText("Please select a base");
    if (!inputDialog->exec())
      QTimer::singleShot(0, this, SLOT(close()));;
    QString item = inputDialog->textValue();
    if (!item.isEmpty())
    {
      index = items.indexOf(item);
    }
    if (index > usbdevs.size())
      index = 0;

    usbdev = usbdevs[index];
  }

  if (tag.Attach(usbdev))
  {
    std::string str;
    int size;
    Status status;
    //Info info;
    tag.GetTagInfo(info);

    tag.GetConfig(config);
    tag.GetStatus(status);

    if (status.state() != IDLE) 
    {
        qInfo() << "Tag not in idle state";
        Detach();
        return false;
    }

    // setup UI

    ui.attachButton->setEnabled(false);
    ui.detachButton->setEnabled(true);
    ui.streamCheckBox->setEnabled(true);
    ui.streamCheckBox->setChecked(false);
    ui.saveButton->setEnabled(true);
    ui.loadButton->setEnabled(true);

    magnetic.clear();
    ui.graphWidget->reset();
    //ui.graphWidget->drawSphere(50.0);
    on_loadButton_clicked();
    qInfo() << "Attach succeeded";
    return true;
  }
  qInfo() << "Attach failed";
  return false;
}

// Detach from tag

void MainWindow::Detach()
{
  timer.stop();
  qualitytimer.stop();
  tag.Detach();
  ui.attachButton->setEnabled(true);
  ui.detachButton->setEnabled(false);
  ui.streamCheckBox->setChecked(false);
  ui.streamCheckBox->setEnabled(false);
  ui.saveButton->setEnabled(false);
  ui.loadButton->setEnabled(false);
  isCalibrating = false;
  isOrienting = false;
  isStreaming = false;
}

void MainWindow::on_streamCheckBox_toggled(bool checked){
  qInfo() << "Stream checkbox " << checked;
  if(checked){
    tag.SetRtc();
    tag.Calibrate();
    timer.start(100);
    isStreaming = true;
    ui.startButton->setEnabled(true);
    ui.stopButton->setEnabled(false);
    ui.clearButton->setEnabled(true);
    ui.saveButton->setEnabled(false);
    ui.loadButton->setEnabled(false);
  } else {
    tag.Stop();
    timer.stop();
    qualitytimer.stop();
    isStreaming = false;
    ui.startButton->setEnabled(false);
    ui.stopButton->setEnabled(false);
    ui.clearButton->setEnabled(false);
    ui.saveButton->setEnabled(true);
    ui.loadButton->setEnabled(true);
  }
}

void MainWindow::on_attachButton_clicked(){
  Attach();
}

void MainWindow::on_detachButton_clicked(){
  Detach();
}

// While tag is attached and streaming is enabled, this
// method is called at regular intervals

void MainWindow::TriggerUpdate(void)
{
  Status status;
  Ack ack;
  static bool done;
  float mx,my,mz,ax,ay,az;
  float pitch, roll, yaw, dip, field;
  
  if (isStreaming)
  { 
    tag.GetCalibrationLog(ack);
    if (ack.has_calibration_log()) {
        for(auto const &sdata : ack.calibration_log().data())
        {
          if (sdata.has_mag()){
              mx = sdata.mag().mx();
              my = sdata.mag().my();
              mz = sdata.mag().mz();
          } else {
            continue;
          }
              //qInfo() << x << "," << y << "," << z;

          if (sdata.has_accel()){
            ax = sdata.accel().ax();
            ay = sdata.accel().ay();
            az = sdata.accel().az();
            if (magnetic.eCompass(mx,my,mz,ax,ay,az,yaw,pitch,roll,dip,field)) {
              ui.yawEdit->setText(QString::asprintf("%.0f",yaw));
              ui.pitchEdit->setText(QString::asprintf("%.0f",pitch));
              ui.rollEdit->setText(QString::asprintf("%.0f",roll));
              ui.dipEdit->setText(QString::asprintf("%.0f",dip));
              ui.fieldEdit->setText(QString::asprintf("%.0f",field));
              ui.hi_graphicsView->setHeading(yaw);
              ui.hi_graphicsView->redraw();
              rotateImage(yaw,pitch,roll);
            }
          }

          if (isCalibrating) {
            magnetic.addData(mx,my,mz);
            //ui.graphWidget->addData(QVector3D(mx,my,mz));
            ui.graphWidget->addPoint(QVector3D(mx,my,mz));
          }
        }
    }   
  } 
}

// called from compassdata (magnetic) when data is added

void MainWindow::calibration_update(void)
{
  QScatterDataArray data;
  float B;
  float V[3];
  float A[3][3];
  //qInfo() << "Calibration update in mainwindow";

  magnetic.getCalibrationConstants(&B, V, A);

  ui.bLabel->setText(QString::asprintf("%.2f",B));

  ui.a0Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[0][0],A[0][1],A[0][2]));
  ui.a1Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[1][0],A[1][1],A[1][2]));
  ui.a2Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[2][0],A[2][1],A[2][2]));

  ui.v0Label->setText(QString::asprintf("%+.3f",V[0]));
  ui.v1Label->setText(QString::asprintf("%+.3f",V[1]));
  ui.v2Label->setText(QString::asprintf("%+.3f",V[2]));
  
  float gaps, variance, wobble, fiterror;
  magnetic.calibrationQuality(gaps, variance, wobble, fiterror);
  ui.qualityLabel->setText(QString::asprintf("%2.1f%%   %2.1f%%      %2.1f%%      %2.1f%%",gaps,variance,wobble,fiterror));
  magnetic.getData(data);
  //ui.graphWidget->setData(data);
  QList<QVector3D> points;
  for (QScatterDataItem item : data){
    points.append(item.position());
  }
  ui.graphWidget->setPoints(points);
  ui.graphWidget->setField(magnetic.getField());
}

void MainWindow::TriggerQualityUpdate()
{
  magnetic.qualityUpdate(); 
}


//  Calibration data collection

void MainWindow::on_clearButton_clicked()
{
  //qInfo() << "clear clicked";
  ui.graphWidget->reset();
  magnetic.clear();
}

void MainWindow::on_startButton_clicked(){
  if (isStreaming) {
    isCalibrating = true;
  //qInfo() << "connect clicked";
    ui.graphWidget->setFocusQuaternion(QQuaternion(1.0,0.0,0.0,0.0));
    //QScatterDataArray data;
    //magnetic.getRegionData(data,50.0);
    //ui.graphWidget->setRegionData(data);
    ui.startButton->setEnabled(false);
    ui.stopButton->setEnabled(true);
    ui.clearButton->setEnabled(false);
    qualitytimer.start(200);
  }
}

  
void MainWindow::on_stopButton_clicked(){
  isCalibrating = false;
  qualitytimer.stop();
  ui.startButton->setEnabled(true);
  ui.stopButton->setEnabled(false);
  ui.clearButton->setEnabled(true);
}

// save/restore calibration

void MainWindow::on_saveButton_clicked(){
  CalibrationConstants constants;
  CalibrationConstants_MagConstants magconstants;
  Ack ack;

  float B;
  float V[3];
  float A[3][3];
  if (magnetic.getCalibrationConstants(&B, V, A)){
    magconstants.set_b(B);
    magconstants.set_v0(V[0]);
    magconstants.set_v1(V[1]);
    magconstants.set_v2(V[2]);
    magconstants.set_a00(A[0][0]);
    magconstants.set_a01(A[0][1]);
    magconstants.set_a02(A[0][2]);
    magconstants.set_a10(A[1][0]);
    magconstants.set_a11(A[1][1]);
    magconstants.set_a12(A[1][2]);
    magconstants.set_a20(A[2][0]);
    magconstants.set_a21(A[2][1]);
    magconstants.set_a22(A[2][2]);
    constants.set_allocated_magnetometer(new ::CalibrationConstants_MagConstants(magconstants));
    constants.set_timestamp(QDateTime::currentSecsSinceEpoch());
    if (!tag.WriteCalibration(constants))
    {
      qInfo() << "WriteCalibration failed";
    }
    //qInfo() << magconstants.DebugString();

  } else {
    qInfo() << "save failed";
  }

}

void MainWindow::on_loadButton_clicked(){
   Ack ack;
   float B;
   float V[3];
   float A[3][3];

   if (tag.ReadCalibration(ack,-1)
        && ack.has_calibration_constants() 
        && ack.calibration_constants().has_magnetometer())
   {
      const CalibrationConstants_MagConstants mag = ack.calibration_constants().magnetometer();
      B = mag.b();
      V[0] = mag.v0();
      V[1] = mag.v1();
      V[2] = mag.v2();
      A[0][0] = mag.a00();
      A[0][1] = mag.a01();
      A[0][2] = mag.a02();
      A[1][0] = mag.a10();
      A[1][1] = mag.a11();
      A[1][2] = mag.a12();
      A[2][0] = mag.a20();
      A[2][1] = mag.a21();
      A[2][2] = mag.a22();
      magnetic.setCalibrationConstants(B,V,A);
      magnetic.getCalibrationConstants(&B, V, A);

      ui.bLabel->setText(QString::asprintf("%.2f",B));

      ui.a0Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[0][0],A[0][1],A[0][2]));
      ui.a1Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[1][0],A[1][1],A[1][2]));
      ui.a2Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[2][0],A[2][1],A[2][2]));

      ui.v0Label->setText(QString::asprintf("%+.3f",V[0]));
      ui.v1Label->setText(QString::asprintf("%+.3f",V[1]));
      ui.v2Label->setText(QString::asprintf("%+.3f",V[2]));

      qInfo() << "Read timestamp " << ack.calibration_constants().timestamp();
     
   } else {
      qInfo() << "Read calibration failed";
   }



}

// Logging of error messages

void MainWindow::logWindowInit(void)
{

    QStringList ll;

    // don't include LOG_FATAL in choices

    for (int i = 0; i < LOG_FATAL; i++) {
        ll << log_level_string(i);
    }

    ui.loglevelBox->addItems(ll);
    ui.loglevelBox->setCurrentIndex(LOG_INFO);

    // connect log text edit box to error logging system

    s_textEdit = ui.logTextEdit;
    qInstallMessageHandler(myMessageOutput);

}

void MainWindow::on_loglevelBox_currentIndexChanged(int index)
{
  log_set_level(index);
  log_level = index;
}

// Save the log window contents as a text file

void MainWindow::on_logsaveButton_clicked()
{
  QFileDialog fd;

  fd.setDirectory(QDir::homePath());
  QString nameFile = fd.getSaveFileName();
  if (nameFile != "")
  {
    QFile file(nameFile);

    if (file.open(QIODevice::ReadWrite))
    {
      QTextStream stream(&file);
      stream << ui.logTextEdit->toPlainText();
      file.flush();
      file.close();
    }
    else
    {
      QMessageBox::critical(this, tr("Error"), tr("Cannot open file"));
      return;
    }
  }
}

void MainWindow::on_logclearButton_clicked()
{
  ui.logTextEdit->clear();
}

void MainWindow::rotateImage(float yaw, float pitch, float roll){
    QMetaObject::invokeMethod(rootObject, "setRotation",
        //Q_RETURN_ARG(QString, returnedValue),
        Q_ARG(QVariant, yaw),
        Q_ARG(QVariant, pitch),
        Q_ARG(QVariant, roll));

}





