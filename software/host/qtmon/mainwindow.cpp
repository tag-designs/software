
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
#include <ctime>
#include <fstream>
#include <iomanip>

#include "flashdownload.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "logscreen.h"

#include <taglogs.h>
#include "configtab.h"

extern "C"
{
#include "log.h"
}

extern void myMessageOutput(QtMsgType type, const QMessageLogContext &context,
                            const QString &msg);

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
  ui->setupUi(this);
  this->setAttribute(Qt::WA_AlwaysShowToolTips, true);

  // create config and log tab widgets

  configtab_ = new ConfigTab(this);
  ui->configTab->layout()->addWidget(configtab_);

  logtab = new LogScreen(this);
  ui->logTab->layout()->addWidget(logtab);

  // Connect signals & slots

  // connect to buttons on log tab
  connect(logtab, &LogScreen::printConfig_clicked, this, &MainWindow::on_LogConfigButton_clicked);
  connect(logtab, &LogScreen::printTagConfig_clicked, this, &MainWindow::on_LogTagConfigButton_clicked);
  // connect to buttons config tab
  connect(configtab_, &ConfigTab::start_clicked, this, &MainWindow::Start);
  connect(configtab_, &ConfigTab::config_restore_clicked, this, &MainWindow::SetConfigFromTag);

  ui->mainTabWidget->setCurrentIndex(0);

  // Tag state poll timer

  connect(&timer, SIGNAL(timeout()), this, SLOT(TriggerUpdate()));

  // Attach to tag

  on_Attach_clicked();
}

MainWindow::~MainWindow()
{
  configtab_->Detach();
  tag.Detach();
  delete ui;
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

    // Initialize tag tab with tag info

    TagInfo info;
    tag.GetTagInfo(info);
    tag.GetConfig(config);
    tag.GetStatus(status);

    ui->info_tagtype->setText(
        QString::fromStdString(TagType_Name(info.tag_type())));
    ui->info_boardname->setText(QString::fromStdString(info.board_desc()));
    ui->info_firmware->setText(QString::fromStdString(info.firmware()));
    ui->info_gitHash->setText(QString::fromStdString(info.githash()));
    ui->info_gitUrl->setText(QString::fromStdString(info.gitrepo()));
    ui->info_uuid->setText(QString::fromStdString(info.uuid()));
    ui->info_flash->setText(QString::number(info.intflashsz()) + "KB");
    ui->info_buildDate->setText(QString::fromStdString(info.build_time()));
    ui->info_srcpath->setText(QString::fromStdString(info.source_path()));
    TestResult result = status.test_status();

    ui->info_testStatus->setText(QString::fromStdString(TestResult_Name(result)));

    logtab->setEnabled(true);

    // handle tag type specific Status Page setup

    if (config.tag_type() == TagType::BITTAG)
    {
      ui->ExternalLog->setHidden(true);
    }
    else
    {
      ui->ExternalLog->setHidden(false);
    }
    configtab_->Attach(config);
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

    timer.stop();
    TriggerUpdate();
    return false;
  }
}

// While tag is attached, this
// method is called at regular intervals

void MainWindow::TriggerUpdate(void)
{
  // check the state & voltage
  // tag state change affects UI state

  Status status;

  if (tag.IsAttached())
  {
    std::string msg;

    float voltage;
    if (tag.Voltage(voltage))
    {
      ui->info_Voltage->setText(
          QString::number(static_cast<double>(voltage), 'f', 2));
    }

    if (tag.GetStatus(status))
    {
      ui->State->setText(QString::fromStdString(TagState_Name(status.state())));
      ui->internalCount->setText(QString::number(status.internal_data_count()));
      ui->externalCount->setText(QString::number(status.external_data_count()));

      double timeerr = QDateTime::currentMSecsSinceEpoch();
      timeerr = status.millis() - timeerr;
      ui->timeError->setText(QString::number(timeerr / 1000.0, 'f', 2));
      ui->info_testStatus->setText(QString::fromStdString(TestResult_Name(status.test_status())));

      // check the clock error

      // State Change

      //  --> IDLE
      if (status.state() == IDLE)
      {

        // tag tab

        ui->syncButton->setEnabled(true);
        ui->testButton->setEnabled(true);
      }
      else
      {
        // tag tab
        ui->syncButton->setEnabled(false);
        ui->testButton->setEnabled(false);
      }

      // config tab
      if (status.state() != current_state)
      {
        configtab_->StateUpdate(status.state());

        if ((status.state() == RUNNING) || (status.state() == CONFIGURED) ||
            (status.state() == HIBERNATING))
        {
          ui->stopButton->setEnabled(true);
          ui->eraseButton->setEnabled(false);
        }
        else
        {
          ui->stopButton->setEnabled(false);
          ui->eraseButton->setEnabled((status.state() != IDLE) &&
                                      (status.state() != TEST));
        }

        if ((status.state() == ABORTED) ||
            (status.state() == FINISHED))
        {
          // tab tag
          ui->datadownloadgroupBox->setEnabled(true);
        }
        else
        {
          // tab tag
          ui->datadownloadgroupBox->setEnabled(false);
        }
      }
      current_state = status.state();
    }
  }
}

void MainWindow::Start()
{
  Config config;
  if (configtab_->GetConfig(config))
  {

    //qDebug() << QString::fromStdString(config.DebugString());
    if (!tag.Start(config))
    {
      QMessageBox msgBox;
      msgBox.setWindowTitle("Error");
      msgBox.setText("Start Failed");
      msgBox.setStandardButtons(QMessageBox::Ok);
      msgBox.exec();
    }
  }
}

void MainWindow::SetConfigFromTag()
{
  Config config;
  if (tag.GetConfig(config))
    configtab_->SetConfig(config);
}

/********************************************
 *        Status Tab
 ********************************************/

void MainWindow::on_Attach_clicked()
{
  if (Attach())
  {
    ui->StatusGroup->setEnabled(true);
    ui->TagInformation->setEnabled(true);
    ui->ControlGroup->setEnabled(true);
    configtab_->StateUpdate(current_state);
    ui->Attach->setEnabled(false);
    ui->Detach->setEnabled(true);
  }
}

void MainWindow::on_Detach_clicked()
{
  tag.Detach();
  timer.stop();
  TriggerUpdate();
  logtab->clear();
  ui->Attach->setEnabled(true);
  ui->Detach->setEnabled(false);
  ui->StatusGroup->setEnabled(false);
  ui->TagInformation->setEnabled(false);
  ui->ControlGroup->setEnabled(false);
  configtab_->Detach();
  logtab->setEnabled(false);
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
    ui->testButton->setEnabled(false);
    ui->info_testStatus->setText("Running");
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
  // if (ret == QMessageBox::Cancel) qInfo() << "Erase Flash: Cancel";
  if (ret == QMessageBox::Ok)
  {
    //qInfo() << "Erase Flash: Ok";
    if (!tag.Erase())
    {
      qDebug() << "tag reset returned false";
    }
  }
}

// download tag data --  reads stm32 program flash

void MainWindow::on_internalDownloadButton_clicked()
{
  Status status;
  std::string str;
  if (!tag.GetStatus(status))
    return;

  Config config;
  tag.GetConfig(config);
  TagInfo info;

  // open an output file

  QFileDialog fd;
  fd.setNameFilter(tr("Binary (*.txt)"));
  fd.setFileMode(QFileDialog::AnyFile);
  fd.setDirectory(QDir::homePath());
  QString fileName = fd.getSaveFileName();

  if (fileName == "")
    return;
  //QFile file(fileName);

  std::fstream fs;
  fs.open(fileName.toStdString(), std::fstream::out);
  if (!fs.is_open())
  {
    qDebug() << "couldn't open %s" << fileName;
    return;
  }

  dumpTagLogHeader(fs, tag, tag_log_output_txt);

  // Dump Data

  QProgressDialog progress("Downloading data...", "Cancel", 0, status.internal_data_count(), this);
  progress.setWindowModality(Qt::WindowModal);

  int total = 0;
  int len = 0;

  Ack ack;
  do
  {
    qApp->processEvents();
    ack.Clear();
    len = 0;
    if (tag.GetDataLog(ack, total))
    {
      //if (ack.has_error_message()) {
      if (ack.error_message() != "") {
        log_error(ack.error_message().c_str());
      }
      len = dumpTagLog(fs, ack, config, tag_log_output_txt);
      if (len == 0) {
        log_error("no data\n");
      } else if (len == -1) {
         log_error("no matching log type\n");
      } else {
      total += len;
      log_error("downloaded %d blocks\n",len);
      }
      if (status.internal_data_count())
        progress.setValue(total);// % status.internal_data_count());
    } 
  } while (len && !progress.wasCanceled());
  fs.close();
  return;
}

/********************************************
 *                Log Tab
 ********************************************/

// these help with debugging UI
// we should disable them in release

void MainWindow::on_LogConfigButton_clicked()
{
  Config tmp;
  configtab_->GetConfig(tmp);
  logtab->append(QString::fromStdString(tmp.DebugString()));
}

void MainWindow::on_LogTagConfigButton_clicked()
{
  Config tmp;
  if (tag.GetConfig(tmp))
  {
    logtab->append(QString::fromStdString(tmp.DebugString()));
  }
  else
  {
    qDebug() << "tag.GetConfig() returned false";
  }
}
