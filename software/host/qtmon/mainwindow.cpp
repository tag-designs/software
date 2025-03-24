
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

//#include "taglogs.h"
#include "configtab.h"
#include "download.h"




// main window

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  ui.setupUi(this);
  this->setAttribute(Qt::WA_AlwaysShowToolTips, true);

  // start on main tab

  ui.mainTabWidget->setCurrentIndex(0);

  // Change Main Window title

  QString title = QString::fromStdString("Tag Monitor v") + QString::number(version);

  setWindowTitle(title);

  // Tag state poll timer

  connect(this, SIGNAL(StateUpdate(TagState)), ui.configtab, SLOT(StateUpdate(TagState)));
  connect(&timer, SIGNAL(timeout()), this, SLOT(TriggerUpdate()));

  // Attach to tag

  on_Attach_clicked();

}

MainWindow::~MainWindow()
{
  ui.configtab->Detach();
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
    //inputDialog->setOption(QInputDialog::NoButtons);
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
    tag.GetConfig(config);
    tag.GetStatus(status);

    // check qtmonitor version 

    float min_version = 2.0;//info.qtmonitor_min_version();

    if (min_version > version) {
      QMessageBox msgBox;
      QString message = QString("monitor version %1 less than required version %2").arg(version).arg(min_version);
      msgBox.setWindowTitle("Warning");
      msgBox.setText(message);
      msgBox.setStandardButtons(QMessageBox::Ok);
      msgBox.exec();
      on_Detach_clicked();
      return false;
    }

   // fill information table

    ui.info_tagtype->setText(
        QString::fromStdString(TagType_Name(info.tag_type())));
    ui.info_boardname->setText(QString::fromStdString(info.board_desc()));
    ui.info_firmware->setText(QString::fromStdString(info.firmware()));
    ui.info_gitHash->setText(QString::fromStdString(info.githash()));
    ui.info_gitUrl->setText(QString::fromStdString(info.gitrepo()));
    ui.info_uuid->setText(QString::fromStdString(info.uuid()));
    ui.info_flash->setText(QString::number(info.intflashsz()) + "KB");
    ui.info_flash_ext->setText(QString::number(info.extflashsz()/(1024*1024))+"MB");
    external_flash_size=info.extflashsz();
    ui.info_buildDate->setText(QString::fromStdString(info.build_time()));
    ui.info_srcpath->setText(QString::fromStdString(info.source_path()));

    // connect log and config tabs

    ui.configtab->Attach(tag);
    ui.configtab->SetConfig(config);
    ui.errorTab->Attach(tag);

    // start the StateUpdate timer
    
    TriggerUpdate();
    timer.start(400);
    return true;
  }
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
      int external_count = status.external_data_count();
      ui.State->setText(QString::fromStdString(TagState_Name(status.state())));
      ui.internalCount->setText(QString::number(status.internal_data_count()));
      if (external_count != 0) {
        ui.externalCount->setText(QString::number(external_count));
        ui.ExternalLog->setVisible(true);
      } else {
        ui.ExternalLog->setVisible(false);
      }

      double timeerr = QDateTime::currentMSecsSinceEpoch();
      timeerr = status.millis() - timeerr;
      ui.timeError->setText(QString::number(timeerr / 1000.0, 'f', 2));
      ui.info_testStatus->setText(QString::fromStdString(TestResult_Name(status.test_status())));
         
      ui.syncButton->setEnabled(status.state() == IDLE);
      ui.testButton->setEnabled(status.state() == IDLE);
      ui.eraseButton->setEnabled((status.state() == FINISHED) || (status.state() == ABORTED));
      ui.datadownloadgroupBox->setEnabled((status.state() == FINISHED) || (status.state() == ABORTED));
      ui.stopButton->setEnabled((status.state() == RUNNING) || (status.state() == HIBERNATING));

      if (status.state() == IDLE)
      {
        emit IdleState();    
      }
     

      if (status.state() == sRESET)
      {
        emit SectorsErased(status.sectors_erased());
      } 

      current_state = status.state();  
      emit StateUpdate(current_state);   
    }
  }
}




/********************************************
 *        Status Tab
 ********************************************/

void MainWindow::on_Attach_clicked()
{
  if (Attach())
  {
    ui.StatusGroup->setEnabled(true);
    ui.TagInformation->setEnabled(true);
    ui.ControlGroup->setEnabled(true);
    ui.Attach->setEnabled(false);
    ui.Detach->setEnabled(true);
  } else {
    ui.StatusGroup->setEnabled(false);
    ui.TagInformation->setEnabled(false);
    ui.ControlGroup->setEnabled(false);
    ui.Attach->setEnabled(true);
    ui.Detach->setEnabled(false);
    ui.datadownloadgroupBox->setEnabled(false);
  }
}

void MainWindow::on_Detach_clicked()
{
  tag.Detach();
  timer.stop();
  TriggerUpdate();
  ui.Attach->setEnabled(true);
  ui.Detach->setEnabled(false);
  ui.StatusGroup->setEnabled(false);
  ui.TagInformation->setEnabled(false);
  ui.ControlGroup->setEnabled(false);
  ui.datadownloadgroupBox->setEnabled(false);
  ui.configtab->Detach();
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
    ui.testButton->setEnabled(false);
    ui.info_testStatus->setText("Running");
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
      if (external_flash_size) {
      QProgressDialog progress("Erasing flash...", "exit dialog", 0, external_flash_size/sector_size, this);
      progress.setWindowModality(Qt::WindowModal);
      connect(this,SIGNAL(SectorsErased(int)),&progress,SLOT(setValue(int)));
      connect(this,SIGNAL(IdleState()), &progress, SLOT(close()));
      progress.exec();
      }
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
  //connect(&dl,&Download::progressRangeChanged, ui.progressBar, &QProgressBar::setRange);
  //connect(&dl,&Download::progressValueChanged, ui.progressBar, &QProgressBar::setValue);

  qDebug() <<  "starting download";

  dl.start(&tag,&fs);

  pd.exec();
  return;
}



