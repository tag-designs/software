
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
#include <QPromise>

#include <ctime>
#include <fstream>
#include <iomanip>

#include <google/protobuf/util/json_util.h>
#include <streambuf>


#include "tagclass.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "tag.pb.h"

#include "download.h"

#include <DisplayManager.h>
#include <CubeProgrammer_API.h>

#define title_string "Tag Programmer v1.0"


// main window

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  ui.setupUi(this);
  this->setAttribute(Qt::WA_AlwaysShowToolTips, true);

  // Change Main Window title

  setWindowTitle(QString(title_string));

  // Tag state poll timer

  connect(&timer, SIGNAL(timeout()), this, SLOT(TriggerUpdate()));

  // attach if possible to collect tag information

  const char* loaderPath = "./.";


    /* Set device loaders path that contains FlashLoader and ExternalLoader folders*/
	setLoadersPath(loaderPath);

    /* Set the progress bar and message display functions callbacks */


  if (Attach())
    tag.Detach();

}

MainWindow::~MainWindow()
{
  tag.Detach();
}

bool MainWindow::Attach()
{
  if (tag.IsAttached())
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Warning");
    msgBox.setText("Already Attached to Tag");
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();
    return false;
  }

  std::vector<UsbDev> usbdevs;
  if (!tag.Available(usbdevs) || (usbdevs.size() == 0))
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Warning");
    msgBox.setText("No Tag Bases Found");
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();
    return false;
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
      return false;
    QString item = inputDialog->textValue();
    if (!item.isEmpty())
    {
      index = items.indexOf(item);
    }
    if (index > usbdevs.size())
      index = 0;
  }

  if (tag.Attach(usbdevs[index]))
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

    if (min_version > version) {
      /*
      QMessageBox msgBox;
      QString message = QString("monitor version %1 less than required version %2").arg(version).arg(min_version);
      msgBox.setWindowTitle("Warning");
      msgBox.setText(message);
      msgBox.setStandardButtons(QMessageBox::Ok);
      msgBox.exec();
      */
      tag.Detach();
      return false;
    }

   // fill information table

    ui.info_tagtype->setText(
        QString::fromStdString(TagType_Name(info.tag_type())));
    ui.info_firmware->setText(QString::fromStdString(info.firmware()));
    ui.info_gitHash->setText(QString::fromStdString(info.githash()));
    ui.info_uuid->setText(QString::fromStdString(info.uuid()));
    ui.info_buildDate->setText(QString::fromStdString(info.build_time()));

    // start the StateUpdate timer
    
    TriggerUpdate();
    timer.start(400);
    return true;
  }
  /*
  else
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Warning");
    msgBox.setText("Could not attach to tag");
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();

    //timer.stop();
    //TriggerUpdate(); // should this be here?
    return false;
  }
  */
 return false;
}

// While tag is attached, this
// method is called at regular intervals

void MainWindow::TriggerUpdate(void)
{

  Status status;

  if (tag.IsAttached())
  {
    std::string msg;

    float voltage;
    if (tag.Voltage(voltage))
    {
      ui.info_Voltage->setText(
          QString::number(static_cast<double>(voltage), 'f', 2));
    }

    // update status data

    if (tag.GetStatus(status))
    {
      //int external_count = status.external_data_count();
      ui.State->setText(QString::fromStdString(TagState_Name(status.state())));
  
  

      double timeerr = QDateTime::currentMSecsSinceEpoch();
      timeerr = status.millis() - timeerr;
      ui.timeError->setText(QString::number(timeerr / 1000.0, 'f', 2));
      ui.info_testStatus->setText(QString::fromStdString(TestResult_Name(status.test_status())));

      if (status.state() == IDLE)
      {
        //ui.syncButton->setEnabled(true);
        //ui.testButton->setEnabled(true);
        ui.progressBar->setVisible(false);

        // config tab

        //ui.tagConfigGroup->setEnabled(true);
        //ui.configRestoreButton->setEnabled(true);
       
      }
      else
      {

        //ui.syncButton->setEnabled(false);
        //ui.testButton->setEnabled(false);

        if (status.state() == sRESET)
        {
          ui.progressBar->setValue(status.sectors_erased());;
        } 

        if (status.state() != current_state)
        {
          if ((status.state() == RUNNING) || (status.state() == CONFIGURED) ||
              (status.state() == HIBERNATING))
          {
            //ui.stopButton->setEnabled(true);
            //ui.eraseButton->setEnabled(false);
          }
          else
          {
            //ui.stopButton->setEnabled(false);
            //ui.eraseButton->setEnabled((status.state() != IDLE) &&
            //                            (status.state() != TEST) &&
             //                           (status.state() != sRESET));
          }

          if ((status.state() == ABORTED) ||
              (status.state() == FINISHED))
          {
            //ui.tagLogSaveButton->setEnabled(true);
          }
          else
          {
              //ui.tagLogSaveButton->setEnabled(false);
          }
        
        }
      }
      current_state = status.state();
      // broadcast state change
     
    }
  }
  //emit StateUpdate(current_state);
}




/********************************************
 *        Status Tab
 ********************************************/
/*
void MainWindow::on_Attach_clicked()
{
  if (Attach())
  {
    ui.StatusGroup->setEnabled(true);
    ui.TagInformation->setEnabled(true);
    //ui.ControlGroup->setEnabled(true);
    //ui.Attach->setEnabled(false);
    //ui.Detach->setEnabled(true);
  } else {
    ui.StatusGroup->setEnabled(false);
    ui.TagInformation->setEnabled(false);
    //ui.ControlGroup->setEnabled(false);
    //ui.Attach->setEnabled(true);
    //ui.Detach->setEnabled(false);
    //ui.tagLogSaveButton->setEnabled(false);
  }
  ui.progressBar->setVisible(false);
}

void MainWindow::on_Detach_clicked()
{
  tag.Detach();
  timer.stop();
  TriggerUpdate();
  //ui.Attach->setEnabled(true);
  //ui.Detach->setEnabled(false);
  //ui.StatusGroup->setEnabled(false);
  ui.TagInformation->setEnabled(false);
  //ui.ControlGroup->setEnabled(false);
}


void MainWindow::on_syncButton_clicked()
{
  tag.SetRtc();
}

void MainWindow::on_stopButton_clicked()
{
  tag.Stop();
}

void MainWindow::on_testButton_clicked()
{
  if (current_state == IDLE)
  {
    std::string msg;
    tag.Test(RUN_ALL); // need to check return !
    //ui.testButton->setEnabled(false);
    //ui.info_testStatus->setText("Running");
  }
}

void MainWindow::on_eraseButton_clicked()
{
  QMessageBox msgBox;
  msgBox.setWindowTitle("Reset Tag");
  msgBox.setText("Erase tag state and data ?");
  msgBox.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
  msgBox.setDefaultButton(QMessageBox::Cancel);
  int ret = msgBox.exec();
  if (ret == QMessageBox::Ok)
  {
    if (!tag.Erase())
    {
      qDebug() << "tag reset returned false";
    } else {
      ui.progressBar->setMaximum(1000);
      ui.progressBar->setValue(0);
      ui.progressBar->setVisible(true);
    }
  }
}

// download tag data  -- should most of this be moved to the
// Download class?

void MainWindow::on_tagLogSaveButton_clicked()
{
  Download dl;

  QFileDialog fd;
  fd.setNameFilter(tr("Binary (*.txt)"));
  fd.setFileMode(QFileDialog::AnyFile);
  fd.setDirectory(QDir::homePath());
  fd.setDefaultSuffix("txt");
  QString fileName = fd.getSaveFileName();

  if (fileName.isNull()) {
      return;
  }

  std::fstream fs;
  fs.open(fileName.toStdString(), std::fstream::out);
  if (!fs.is_open())
  {
    qDebug() << "couldn't open %s" << fileName;
    return;
  }

  qDebug() <<  "connecting progess dialog";

  // Create Progress Dialog

  QProgressDialog pd = QProgressDialog("Downloading ..","Cancel",0,0);

  connect(&dl,&Download::progressRangeChanged, &pd, &QProgressDialog::setRange);
  connect(&dl,&Download::progressValueChanged, &pd, &QProgressDialog::setValue);
  connect(&dl,&Download::downloadFinished, &pd, &QProgressDialog::cancel);
  connect(&pd,&QProgressDialog::canceled,&dl,&Download::cancel);
  connect(&dl,&Download::progressRangeChanged, ui.progressBar, &QProgressBar::setRange);
  connect(&dl,&Download::progressValueChanged, ui.progressBar, &QProgressBar::setValue);

  ui.progressBar->setVisible(true);

  qDebug() <<  "starting download";

  dl.start(&tag,&fs);

  pd.exec();

  ui.progressBar->setVisible(false);
  return;
}

*/

void MainWindow::on_programButton_clicked(){
  qDebug() << qgetenv("STM32CUBEPROGRAMMER");
  qDebug() << "run program";
  if (current_state != IDLE){
    if (current_state == ABORTED || current_state == FINISHED){
      QMessageBox msgBox;
      msgBox.setWindowTitle("Tag state is not erased");
      msgBox.setText("Erase tag state and data ?");
      msgBox.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
      msgBox.setDefaultButton(QMessageBox::Cancel);
      int ret = msgBox.exec();
      if (ret == QMessageBox::Ok)
      {
        if (!tag.Erase())
        {
          qDebug() << "tag reset returned false";
        } else {
          ui.progressBar->setMaximum(1000);
          ui.progressBar->setValue(0);
          ui.progressBar->setVisible(true);
        }
      } else {
        tag.Detach();
        return; 
      }
    }
  }
  tag.Detach();
}



void MainWindow::on_fileSelectButton_clicked()
{
 
  QString filename = QFileDialog::getOpenFileName(this,tr("binary file to program"), 
                                                 QDir::homePath(),
                                                 tr("Binary (*.bin *.elf)"));
  if (!filename.isNull())
    ui.fileName->setText(filename);
}

//void MainWindow::lBar(int currProgress, int total){}
//void MainWindow::DisplayMessage(int msgType, QString str){}
//void MainWindow::logMessage(int msgType, QString str){}