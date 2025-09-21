
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
#include <QtDataVisualization>
#include <QtDataVisualization/Q3DScatter>
#include <QtDataVisualization/QtDataVisualization>
#include <Q3DScatterWidgetItem>
#include <QtGraphs/qscatter3dseries.h>
#include <QtGraphs/qscatterdataproxy.h>

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

  // Tag state poll timer

  connect(&timer, SIGNAL(timeout()), this, SLOT(TriggerUpdate()));

  scatterGraphInit();
  
  if (Attach())
    tag.Detach();
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
    qDebug() << "Already attached to tag";
    return false;
  }

  if (tag.Attach(usbdev))
  {
    std::string str;
    int size;
    Config config;
    Status status;
    TagInfo info;
    tag.GetTagInfo(info);
    //tag.GetConfig(config);
    tag.GetStatus(status);

    // check qtmonitor version 

    float min_version = info.qtmonitor_min_version();

    if (0) { //min_version > version) {
      tag.Detach();
      return false;
    }
    
    ui.connectButton->setEnabled(false);
    ui.disconnectButton->setEnabled(true);
    TriggerUpdate();
    timer.start(400);
    return true;
  }
  qDebug() << "Attach failed";
 return false;
}

void MainWindow::Detach()
{
  timer.stop();
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


}

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

void MainWindow::scatterGraphInit(){


}

void MainWindow::on_addButton_clicked()
{
  float x = generator->generateDouble()*5000.0-2500.0;
  float y = generator->generateDouble()*5000.0-2500.0;
  float z = generator->generateDouble()*5000.0-2500.0;
  ui.graphWidget->addData(x,y,z);
}