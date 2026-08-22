
#include <QMainWindow>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTime>
#include <QThread>
#include <QTimer>
#include <QLayout>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <QPromise>

#include <ctime>
#include <fstream>
#include <iomanip>
#include <utility>

#include <google/protobuf/util/json_util.h>
#include <streambuf>


#include "tagclass.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qtfiledialog.h"

#include "tag.pb.h"

#include "tagconfiguration/configtab.h"
#include "abstractdownload.h"
#include "taglogwriter.h"


namespace
{

// Fallback for older IMUTag firmware that predates
// Status.erase_sectors_total_plus_one. New firmware reports the erase total
// directly, so the UI does not need tag-specific page geometry.
constexpr int kImuDataLogPageBytes = 2048;

int roundedSectorCount(qint64 bytes, int sector_size)
{
  if (bytes <= 0 || sector_size <= 0)
    return 0;
  return static_cast<int>((bytes + sector_size - 1) / sector_size);
}

bool isDownloadableState(TagState state)
{
  // EXCEPTION is no longer collecting data, so allow users to reach the
  // download controls instead of requiring a reset that could erase evidence.
  return state == FINISHED || state == ABORTED || state == EXCEPTION;
}

} // namespace

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

int MainWindow::eraseSectorMaximum(const Status &status) const
{
  if (status.erase_sectors_total_plus_one() > 0)
    return status.erase_sectors_total_plus_one() - 1;

  if (tag_type == IMUTAG)
    return roundedSectorCount(
        static_cast<qint64>(status.external_data_count()) * kImuDataLogPageBytes,
        sector_size);

  return roundedSectorCount(external_flash_size, sector_size);
}

void MainWindow::logTagDebugMessage(const QString &message)
{
  for (const QChar ch : message) {
    if (ch == QLatin1Char('\r'))
      continue;
    if (ch == QLatin1Char('\n')) {
      qDebug().noquote() << "Log:" << tag_debug_line_buffer;
      tag_debug_line_buffer.clear();
      continue;
    }
    tag_debug_line_buffer.append(ch);
  }
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
    tag_debug_line_buffer.clear();
    tag.GetTagInfo(info);
    tag.GetConfig(config);
    tag.GetStatus(status);
    if (!status.debug_message().empty()){
      logTagDebugMessage(QString::fromStdString(status.debug_message()));
    }

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
    tag_type = info.tag_type();

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
      ui.StatusGroup->setEnabled(true);
      ui.ControlGroup->setEnabled(true);
      int external_count = status.external_data_count();
      ui.State->setText(QString::fromStdString(TagState_Name(status.state())));
      ui.internalCount->setText(QString::number(status.internal_data_count()));
      if (external_count != 0) {
        ui.externalCount->setText(QString::number(external_count));
        ui.ExternalLog->setVisible(true);
      } else {
        ui.ExternalLog->setVisible(false);
      }

      if (!status.debug_message().empty()){
        logTagDebugMessage(QString::fromStdString(status.debug_message()));
      }

      double timeerr = QDateTime::currentMSecsSinceEpoch();
      timeerr = status.millis() - timeerr;
      ui.timeError->setText(QString::number(timeerr / 1000.0, 'f', 2));
      ui.info_testStatus->setText(QString::fromStdString(TestResult_Name(status.test_status())));
         
      ui.syncButton->setEnabled(status.state() == IDLE);
      ui.testButton->setEnabled(status.state() == IDLE);
      ui.calibrateButton->setEnabled(status.state() == IDLE);
      ui.eraseButton->setEnabled((status.state() == FINISHED) ||
                                 (status.state() == ABORTED));
      ui.datadownloadgroupBox->setEnabled(isDownloadableState(status.state()));
      ui.stopButton->setEnabled((status.state() == CONFIGURED) ||
                                (status.state() == RUNNING) ||
                                (status.state() == HIBERNATING) ||
                                (status.state() == CALIBRATE));

      if (status.state() == IDLE)
      {
        emit IdleState();    
      }
     

      if (status.state() == sRESET)
      {
        emit EraseProgress(status.sectors_erased(), eraseSectorMaximum(status));
        emit SectorsErased(status.sectors_erased());
      } 

      if (status.state() == CALIBRATE)
      {
        Ack ack;
        float mx,my,mz,ax,ay,az;
        tag.GetCalibrationLog(ack);
        if (ack.has_calibration_log()) {
            for(auto const &sdata : ack.calibration_log().data())
            {
              bool saw_sensor_data = false;
              if (sdata.has_mag()){
                  mx = sdata.mag().mx();
                  my = sdata.mag().my();
                  mz = sdata.mag().mz();
                  qInfo() << "Mag: " << mx << my << mz;
                  saw_sensor_data = true;
              }

              if (sdata.has_accel()){
                ax = sdata.accel().ax();
                ay = sdata.accel().ay();
                az = sdata.accel().az();
                qInfo() << "Accel: " << ax << ay << az;
                saw_sensor_data = true;
              }

              if (!saw_sensor_data) {
                qInfo() << "Calibration sample had no sensor data";
              }
            }
        }
        else {
          qInfo() << "No calibration log";
        }
      }

      /*
      if ((status.state() == CALIBRATE)  && status.has_sensors()){
        QString sensors = QString::fromStdString(status.sensors().DebugString());
        sensors = sensors.replace("\n",", ");
        qInfo() << "Sensors: " << sensors;
      }*/

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
  tag_debug_line_buffer.clear();
}

void MainWindow::on_syncButton_clicked()
{
  if (!tag.SetRtc()) {
    QMessageBox syncFailedBox;
    syncFailedBox.setWindowTitle(tr("Sync Failed"));
    syncFailedBox.setIcon(QMessageBox::Warning);
    syncFailedBox.setText(tr("The tag did not accept the clock sync command."));
    const std::string message = tag.DebugMessage();
    if (!message.empty())
    {
      syncFailedBox.setInformativeText(QString::fromStdString(message));
      syncFailedBox.setDetailedText(QString::fromStdString(message));
    }
    syncFailedBox.exec();
    return;
  }
  TriggerUpdate();
}

void MainWindow::on_stopButton_clicked()
{
  if (!tag.Stop()) {
    QMessageBox::warning(this,tr("Stop Failed"),
                         tr("The tag did not accept the stop command."));
    return;
  }
  QTimer::singleShot(250,this,[this]() { TriggerUpdate(); });
}

void MainWindow::on_calibrateButton_clicked()
{
  tag.Calibrate();
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
    Status status;
    int erase_sector_maximum = roundedSectorCount(external_flash_size, sector_size);
    if (tag.GetStatus(status))
      erase_sector_maximum = eraseSectorMaximum(status);

    if (!tag.Erase())
    {
      QMessageBox::warning(this,tr("Erase Failed"),
                           tr("The tag did not accept the erase command."));
    } else {
      if (!timer.isActive())
        timer.start(400);

      QProgressDialog progress("Erasing flash...", "Close", 0,
                               erase_sector_maximum, this);
      progress.setWindowModality(Qt::WindowModal);
      progress.setMinimumDuration(0);
      connect(this,&MainWindow::EraseProgress,&progress,
              [&progress](int value, int maximum) {
                if (maximum > progress.maximum())
                  progress.setRange(0, maximum);
                progress.setValue(value);
              });
      connect(this,SIGNAL(SectorsErased(int)),&progress,SLOT(setValue(int)));
      connect(this,SIGNAL(IdleState()), &progress, SLOT(close()));
      progress.exec();
    }
  }
}

// Start a tag-data download from the UI. The window chooses a default storage
// format for the current tag type and owns only the user interaction. The
// download loop and file/database writing are delegated to AbstractDownload and
// the host-lib TagLogWriter implementations.

void MainWindow::on_tagLogSaveButton_clicked()
{
  const TagLogStorageFormat storage_format = defaultTagLogStorageFormat(tag_type);
  const std::vector<TagLogStorageFormat> formats = supportedTagLogStorageFormats(tag_type);
  QStringList filter_list;
  for (TagLogStorageFormat format : formats) {
    filter_list << QString::fromStdString(tagLogFileFilter(format));
  }
  QString selected_filter;
  QString filter = filter_list.join(";;");
  QString initial_path = QDir::homePath()
      + "/untitled"
      + QString::fromStdString(defaultTagLogExtension(storage_format));
  
  QString fileName = HostFileDialog::getSaveFileName(
      this, tr("Save File"), initial_path, filter, &selected_filter);

  if (fileName.isEmpty()) {
      qDebug() << "null filename";
      return;
  }

  TagLogStorageFormat selected_storage_format = storage_format;
  for (TagLogStorageFormat format : formats) {
    if (selected_filter == QString::fromStdString(tagLogFileFilter(format))) {
      selected_storage_format = format;
      break;
    }
  }

  const QString selected_extension =
      QString::fromStdString(defaultTagLogExtension(selected_storage_format));
  const QString selected_suffix = selected_extension.mid(1);
  const QFileInfo file_info(fileName);
  bool suffix_matches_supported_format = false;
  for (TagLogStorageFormat format : formats) {
    const QString suffix = QString::fromStdString(defaultTagLogExtension(format)).mid(1);
    if (file_info.suffix().compare(suffix, Qt::CaseInsensitive) == 0) {
      suffix_matches_supported_format = true;
      break;
    }
  }
  if (file_info.suffix().isEmpty()) {
    fileName += selected_extension;
  } else if (suffix_matches_supported_format
             && file_info.suffix().compare(selected_suffix, Qt::CaseInsensitive) != 0) {
    fileName = file_info.path() + "/" + file_info.completeBaseName() + selected_extension;
  }
  

  qDebug() <<  "connecting progess dialog";

  // Create Progress Dialog

  QProgressDialog *pd = new QProgressDialog("Downloading ..","Cancel",0,0,this);
  pd->setWindowModality(Qt::WindowModal);
  pd->setMinimumDuration(0);

  // This is deliberately an explicit format value instead of a hidden
  // tag-type switch inside AbstractDownload. A future UI can replace the
  // default with a user-selected format for tags that support more than one.
  AbstractDownload *dl = new AbstractDownload(tag, selected_storage_format, fileName.toStdString());
  QThread *download_thread = new QThread(this);
  dl->moveToThread(download_thread);

  connect(download_thread,&QThread::started,dl,&AbstractDownload::exec);
  connect(dl,&AbstractDownload::progressRangeChanged,pd,&QProgressDialog::setRange);
  connect(dl,&AbstractDownload::progressValueChanged,pd,&QProgressDialog::setValue);
  connect(dl,&AbstractDownload::downloadError,this,[this](const QString &message) {
    QMessageBox::critical(this,tr("Download Failed"),message);
  });
  connect(pd,&QProgressDialog::canceled,dl,&AbstractDownload::cancel,Qt::DirectConnection);
  connect(dl,&AbstractDownload::downloadFinished,pd,&QProgressDialog::close);
  connect(dl,&AbstractDownload::downloadFinished,download_thread,&QThread::quit);
  connect(dl,&AbstractDownload::downloadFinished,dl,&QObject::deleteLater);
  connect(download_thread,&QThread::finished,download_thread,&QObject::deleteLater);
  connect(download_thread,&QThread::finished,pd,&QObject::deleteLater);

  const bool restart_status_timer = timer.isActive();
  timer.stop();
  ui.datadownloadgroupBox->setEnabled(false);
  connect(download_thread,&QThread::finished,this,[this,restart_status_timer]() {
    ui.datadownloadgroupBox->setEnabled(tag.IsAttached() &&
                                        isDownloadableState(current_state));
    if (restart_status_timer && tag.IsAttached()) {
      timer.start(400);
      TriggerUpdate();
    }
  });

  qDebug() <<  "starting download";

  download_thread->start();
  pd->show();
  return;
}
