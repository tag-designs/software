
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

#include "taglogs.h"
#include "configtab.h"
#include "download.h"

extern "C"
{
#include "log.h"
}

// hook into the error logging system

extern int log_level;
QTextEdit *s_textEdit = nullptr;

extern void myMessageOutput(QtMsgType type, const QMessageLogContext &context,
                            const QString &msg);

// main window

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
  ui->setupUi(this);
  this->setAttribute(Qt::WA_AlwaysShowToolTips, true);

  // Change Main Window title

  QString title = QString::fromStdString("Tag Monitor v") + QString::number(version);

  setWindowTitle(title);

  // create configuration widgets

  // initialize error log
  // create loglevel choices

  QStringList ll;

    // don't include LOG_FATAL in choices

  for (int i = 0; i < LOG_FATAL; i++) {
    ll << log_level_string(i);
  }

  ui->loglevelBox->addItems(ll);
  ui->loglevelBox->setCurrentIndex(LOG_INFO);

  // connect log text edit box to error logging system

  s_textEdit = ui->logtextEdit;
  qInstallMessageHandler(myMessageOutput);

  ui->mainTabWidget->setCurrentIndex(0);

  // get handle to config tab

  configtab_ = ui->config;

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

    float min_version = info.qtmonitor_min_version();

    if (min_version > version) {
      QMessageBox msgBox;
      QString message = QString("monitor version %1 less than required version %2").arg(version).arg(min_version);
      msgBox.setWindowTitle("Warning");
      msgBox.setText(message);
      msgBox.setStandardButtons(QMessageBox::Ok);
      msgBox.exec();

      //timer.stop();
      //TriggerUpdate();
      return false;
    }

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
    ui->info_flash_ext->setText(QString::number(info.extflashsz()/(1024*1024))+"MB");
    ui->info_buildDate->setText(QString::fromStdString(info.build_time()));
    ui->info_srcpath->setText(QString::fromStdString(info.source_path()));
    TestResult result = status.test_status();

    ui->info_testStatus->setText(QString::fromStdString(TestResult_Name(result)));

    ui->errorLog->setEnabled(true);
    

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

    //ui->loglevelBox->setCurrentIndex(LOG_INFO);
    
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
        ui->progressBar->setVisible(false);
        ui->configControls->setEnabled(true);
      }
      else
      {
        // tag tab
        ui->syncButton->setEnabled(false);
        ui->testButton->setEnabled(false);
        ui->configControls->setEnabled(false);
      }

      if (status.state() == sRESET)
      {
        ui->progressBar->setValue(status.sectors_erased());
        //log_info("Sectors Read %d \n",status.sectors_erased());
      } else {
        //ui->progressBar->setVisible(false);
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
                                      (status.state() != TEST) &&
                                      (status.state() != sRESET));
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

void MainWindow::on_startButton_clicked()
{
  Config config;
  if (configtab_->GetConfig(config))
  {
    if (!tag.Start(config))
    {
      QMessageBox msgBox;
      msgBox.setWindowTitle("Error");
      msgBox.setText("Start Failed");
      msgBox.setStandardButtons(QMessageBox::Ok);
      msgBox.exec();
    }
  } else {
    qDebug() << "on_startButton_clicked failed to get config";
  }
}

void MainWindow::on_tagConfigReadButton_clicked()
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
  } else {
    ui->StatusGroup->setEnabled(false);
    ui->TagInformation->setEnabled(false);
    ui->ControlGroup->setEnabled(false);
    ui->Attach->setEnabled(true);
    ui->Detach->setEnabled(false);
  
    ui->datadownloadgroupBox->setEnabled(false);
  }
  ui->progressBar->setVisible(false);
}

void MainWindow::on_Detach_clicked()
{
  tag.Detach();
  timer.stop();
  TriggerUpdate();
  ui->logtextEdit->clear();
  ui->Attach->setEnabled(true);
  ui->Detach->setEnabled(false);
  ui->StatusGroup->setEnabled(false);
  ui->TagInformation->setEnabled(false);
  ui->ControlGroup->setEnabled(false);
  configtab_->Detach();
  ui->errorLog->setEnabled(false);
  //ui->loglevelBox->setCurrentIndex(LOG_DEBUG);
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
    } else {
      ui->progressBar->setMaximum(1000);
      ui->progressBar->setValue(0);
      ui->progressBar->setVisible(true);
    }
  }
}

// download tag data 

void MainWindow::on_tagLogSaveButton_clicked()
{
  Download dl;

  QFileDialog fd;
  fd.setNameFilter(tr("Binary (*.txt)"));
  fd.setFileMode(QFileDialog::AnyFile);
  fd.setDirectory(QDir::homePath());
  fd.setDefaultSuffix("txt");
  QString fileName = fd.getSaveFileName();

  if (fileName == "") {
      qDebug() << "Empty file name";
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

  connect(&dl,&Download::progressRangeChanged, ui->progressBar, &QProgressBar::setRange);
  connect(&dl,&Download::progressValueChanged, ui->progressBar, &QProgressBar::setValue);

  ui->progressBar->setVisible(true);

  qDebug() <<  "starting download";

  dl.start(&tag,&fs);

  pd.exec();

  ui->progressBar->setVisible(false);

  
  return;
}

/********************************************
 *                Log Tab
 ********************************************/

// these help with debugging UI
// we should disable them in release
/*
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
*/

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
      stream << ui->logtextEdit->toPlainText();
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

/*
 * configuration file operations
*/

void MainWindow::on_configSaveButton_clicked()
{
  QFileDialog fd;
  fd.setFileMode(QFileDialog::AnyFile);
  QString fileName = fd.getSaveFileName(this, tr("Save File"),
                                        QDir::homePath() + "/untitled.json",
                                        tr("Protobuf (*.json)"));
  QString errormsg;

  do
  {
    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = true;
    // deprecated -- options.always_print_primitive_fields = true;
    options.preserve_proto_field_names = true;

    std::ofstream fs(fileName.toStdString());

    if (!fs.is_open())
    {
      errormsg = "Couldn't Open " + fileName;
      break;
    }

    Config configout;
    configtab_->GetConfig(configout);

    std::string json_string;
    if (MessageToJsonString(configout, &json_string, options).ok())
    {
      fs << json_string;
      if (fs.bad())
      {
        errormsg = "Couldn't Write " + fileName;
      }
    }
    else
    {
      errormsg = "Couldn't Create JSON output ";
    }

    fs.close();
  } while (0);

  if (!errormsg.isEmpty())
  {
    QMessageBox msgBox;
    msgBox.setText(errormsg);
    msgBox.exec();
    qDebug() << errormsg;
  }
}

// Restore config from file

void MainWindow::on_configRestoreButton_clicked()
{
  QFileDialog fd;
  QString fileName = fd.getOpenFileName(this, tr("Open File"), QDir::homePath(),
                                        tr("Protobuf (*.json)"));

  if (fileName == "")
    return;
  Config configin;
  std::ifstream fin(fileName.toStdString());

  if (!fin.is_open())
  {
    QMessageBox msgBox;
    msgBox.setText("Couldn't Open " + fileName);
    msgBox.exec();
    qDebug() << "Config file restore couldn't open " << fileName;
    return;
  }

  std::string str((std::istreambuf_iterator<char>(fin)),
                  std::istreambuf_iterator<char>());

  fin.close();
  google::protobuf::util::JsonParseOptions options2;
  if (JsonStringToMessage(str, &configin, options2).ok())
  {
    // We should check the configuration that is read
    // against the current configuration to make sure all
    // the fields are implemented
    configtab_->SetConfig(configin);
  }
  else
  {
    QMessageBox msgBox;
    msgBox.setText("Couldn't Read " + fileName);
    msgBox.exec();
    qDebug() << "Config file restore couldn't read " << fileName;
  }
}
