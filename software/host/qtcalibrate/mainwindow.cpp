
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

  // Tag state poll timer


  connect(&timer, SIGNAL(timeout()), this, SLOT(TriggerUpdate()));
  QObject::connect(&magnetic, &CompassData::calibration_update, this, &MainWindow::calibration_update);
  connect(&qualitytimer, SIGNAL(timeout()), this, SLOT(TriggerQualityUpdate()));
  //calibration_update();

  //if (Attach())
    //tag.Detach();
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
    qInfo() << "magconstant: " << float(info.magconstant());

    ui.connectButton->setEnabled(false);
    ui.disconnectButton->setEnabled(true);
    //TriggerUpdate();
    timer.start(50);
    qualitytimer.start(200);
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
  ui.connectButton->setEnabled(true);
  ui.disconnectButton->setEnabled(false);
  //ui.StatusGroup->setEnabled(false);
  //ui.TagInformation->setEnabled(false);
}

// While tag is attached, this
// method is called at regular intervals

void MainWindow::TriggerUpdate(void)
{
  Status status;

  if (tag.IsAttached())
  {
    tag.GetStatus(status);
    if (status.has_sensors()) {
          SensorData sdata = status.sensors();
          if (sdata.has_mag()){
              float x = sdata.mag().mx();//*info.magconstant();
              float y = sdata.mag().my();//*info.magconstant();
              float z = sdata.mag().mz();//*info.magconstant();
              //qInfo() << x << "," << y << "," << z;

              magnetic.addData(x,y,z);
              ui.graphWidget->addData(x,y,z);

          }
    }

  } else {
    timer.stop();
    qInfo() << "tag not attached";
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


//  Calibration data collection

void MainWindow::on_clearButton_clicked()
{
  ui.graphWidget->clearData();
}

void MainWindow::on_connectButton_clicked(){

  if (Attach()) {
    Status status;
    tag.GetStatus(status);
      if (status.state() == IDLE) {
        on_logclearButton_clicked();
        ui.graphWidget->drawSphere(50.0);
        tag.SetRtc();
        tag.Calibrate();
      } else {
        qInfo() << "Tag not in idle state";
        Detach();
      }
      magnetic.clear();    
  }
}
  
void MainWindow::on_disconnectButton_clicked(){
  tag.Stop();
  Detach();
}


void MainWindow::calibration_update(void)
{
  QScatterDataArray data;
  float B;
  float V[3];
  float A[3][3];

  qInfo() << "Calibration upstate in mainwindow";

  magnetic.calibration_constants(&B, V, A);

  ui.bLabel->setText(QString::asprintf("%.2f",B));

  ui.a0Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[0][0],A[0][1],A[0][2]));
  ui.a1Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[1][0],A[1][1],A[1][2]));
  ui.a2Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[2][0],A[2][1],A[2][2]));

  ui.v0Label->setText(QString::asprintf("%+.3f",V[0]));
  ui.v1Label->setText(QString::asprintf("%+.3f",V[1]));
  ui.v2Label->setText(QString::asprintf("%+.3f",V[2]));
  
  float gaps, variance, wobble, fiterror;
  magnetic.calibration_quality(gaps, variance, wobble, fiterror);
  ui.qualityLabel->setText(QString::asprintf("%2.1f%%   %2.1f%%      %2.1f%%      %2.1f%%",gaps,variance,wobble,fiterror));
  magnetic.getData(data);
  ui.graphWidget->setData(data);

}

void MainWindow::TriggerQualityUpdate()
{
  magnetic.qualityUpdate(); 
}