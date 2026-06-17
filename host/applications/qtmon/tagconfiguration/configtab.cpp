//#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QGroupBox>
// #include <QTextEdit>
// #include <QTime>
// #include <QTimer>
// #include <QtWidgets/QSpacerItem>
// #include <QtWidgets/QSizePolicy>
// #include <ctime>
#include <algorithm>
#include <fstream>
#include <google/protobuf/util/json_util.h>
#include <streambuf>

#include "hibernate.h"
#include "configtab.h"
#include "adxl362config.h"
#include "lsm6dsvconfig.h"
#include "bittaglog.h"
#include "ui_configtab.h"
#include "qtfiledialog.h"

ConfigTab::ConfigTab(QWidget *parent) : QWidget(parent)
{

  ui.setupUi(this);

  // initialize message box

  msgBox.setWindowTitle("Error");
  msgBox.setStandardButtons(QMessageBox::Ok);

  // Build Schedule Tab

  QVBoxLayout *layout = new QVBoxLayout();
  layout->addWidget(&schedule);
  layout->addWidget(&btlog);
  layout->addStretch(1);
  ui.scheduleTab->setLayout(layout);
  //int index = addTab(&scheduleTab,"Schedule");
  //setTabToolTip(index,"Configure Tag Schedule");

  // Build Sensor Tab

  layout = new QVBoxLayout();
  layout->addWidget(&adxl);
  layout->addWidget(&lsm);
  layout->addStretch(1);
  ui.sensorTab->setLayout(layout);
  //index = addTab(&sensorTab,"Sensors");
  //setTabToolTip(index,"Configure Sensors");
  //StateUpdate(STATE_UNSPECIFIED);
}

ConfigTab::~ConfigTab(){}

bool ConfigTab::isActive(){
  return active;
}

static void mergeRestoredConfig(Config &target, const Config &restored)
{
  if (target.has_active_interval() && restored.has_active_interval())
  {
    const Config_Interval &interval = restored.active_interval();
    if ((interval.start_epoch() != 0) || (interval.end_epoch() != 0))
    {
      *target.mutable_active_interval() = interval;
    }
  }

  if ((target.hibernate_size() > 0) && (restored.hibernate_size() > 0))
  {
    const int count = std::min(target.hibernate_size(), restored.hibernate_size());
    for (int i = 0; i < count; i++)
    {
      *target.mutable_hibernate(i) = restored.hibernate(i);
    }
  }

  if ((target.period() != 0) && (restored.period() != 0))
  {
    target.set_period(restored.period());
  }

  if ((target.start_delay() != 0) && (restored.start_delay() != 0))
  {
    target.set_start_delay(restored.start_delay());
  }

  if ((target.bittag_log() != BITTAG_UNSPECIFIED) &&
      (restored.bittag_log() != BITTAG_UNSPECIFIED))
  {
    target.set_bittag_log(restored.bittag_log());
  }

  if (target.has_adxl362() && restored.has_adxl362())
  {
    *target.mutable_adxl362() = restored.adxl362();
  }

  if (target.has_lsm6() && restored.has_lsm6())
  {
    *target.mutable_lsm6() = restored.lsm6();
  }
}

bool ConfigTab::Attach(Tag &t)
{
  tag = &t;

  if (schedule.Attach(t) && btlog.Attach(t) && adxl.Attach(t) &&
      lsm.Attach(t)) {
    return true;
  } else {
    qDebug() << "Attach failed\n";
    return false;
  }
}

void ConfigTab::Detach()
{
  schedule.Detach();
  btlog.Detach();
  adxl.Detach();
  lsm.Detach();
  setEnabled(false);
  setVisible(false);
  active = false;
}

void ConfigTab::StateUpdate(TagState state)
{
  {
    if (state == IDLE ) {
      if (schedule.isActive())
        schedule.setEnabled(true);
      if (btlog.isActive())
        btlog.setEnabled(true);
      if (adxl.isActive())
        adxl.setEnabled(true);
      if (lsm.isActive())
        lsm.setEnabled(true);
      ui.configRestoreButton->setEnabled(true);
      ui.startButton->setEnabled(true);
      ui.readButton->setEnabled(true);
    } else {
      schedule.setEnabled(false);
      btlog.setEnabled(false);
      adxl.setEnabled(false);
      lsm.setEnabled(false);
      ui.configRestoreButton->setEnabled(false);
      ui.startButton->setEnabled(false);
      ui.readButton->setEnabled(false);
    }
  }
  //qDebug() << "StateUpdate";
}

/*****************************************************
 *                Configuration TAB                  *
 ****************************************************/

bool ConfigTab::GetConfig(Config &config)
{

  config = current_config_;
  config.set_tag_type(tag_type_);
  
  // Get configuration from children

  if (schedule.isActive() && !schedule.GetConfig(config)){
    qDebug() << "failed to get schedule config";
    return false;
  }

  if (btlog.isActive() && !btlog.GetConfig(config)){
    qDebug() << "failed to get btlog config";
    return false;
  }

  if (adxl.isActive() && !adxl.GetConfig(config)){
    qDebug() << "failed to get adxl config";
    return false;
  }

  if (lsm.isActive() && !lsm.GetConfig(config)){
    qDebug() << "failed to get lsm config";
    return false;
  }

  current_config_ = config;
  
  return true;

  // Sanity check -- should put this in the schedule class

  // int64_t end = config.active_interval().end_epoch();

  // if (end < QDateTime::currentSecsSinceEpoch())
  // {   
  //   msgBox.setText("Configuration Error: end time < current time");
  //   msgBox.exec();
  //   return false;
  // } else {
  //   return true;
  // }
}

bool ConfigTab::SetConfig(const Config &new_config)
{
  current_config_ = new_config;
  tag_type_ = new_config.tag_type();
  // we need to sanity check the new and old configs !
  // if any of these return false, this should return false
  if (schedule.SetConfig(new_config) &&
      btlog.SetConfig(new_config) &&
      adxl.SetConfig(new_config) &&
      lsm.SetConfig(new_config))
  {
    active = true;
    setVisible(true);
    setEnabled(true);
  } else {
    setVisible(false);
    setEnabled(false);
    active = false;
    qDebug() << "SetConfig failed\n";
  }
  return active;
}

/*
 * configuration file operations
*/

void ConfigTab::on_configSaveButton_clicked()
{
  QString fileName = HostFileDialog::getSaveFileName(
      this, tr("Save File"), QDir::homePath() + "/untitled.json",
      tr("Protobuf (*.json);;All Files (*)"));
  QString errormsg;

  if (fileName.isNull()) {
    return;
  }

  do
  {
    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = true;
    options.always_print_fields_with_no_presence = true;
    options.preserve_proto_field_names = true;

    std::ofstream fs(fileName.toStdString());

    if (!fs.is_open())
    {
      errormsg = "Couldn't Open " + fileName;
      break;
    }

    Config configout;
    GetConfig(configout);

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

void ConfigTab::on_configRestoreButton_clicked()
{
  Config configin;

  QString fileName = HostFileDialog::getOpenFileName(
      this, tr("Open File"), QDir::homePath(),
      tr("Protobuf (*.json);;All Files (*)"));

  if (fileName.isNull())
    return;

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
  auto status = JsonStringToMessage(str, &configin, options2);
  if (status.ok())
  {
    Config merged = current_config_;
    mergeRestoredConfig(merged, configin);
    if (!SetConfig(merged))
    {
      QMessageBox msgBox;
      msgBox.setText("Config file parsed but is not valid for this tag");
      msgBox.exec();
      qDebug().noquote() << "Config file restore parsed but SetConfig failed:"
                         << QString::fromStdString(merged.DebugString());
    }
  }
  else
  {
    QString statusText = QString::fromStdString(status.ToString());
    QMessageBox msgBox;
    msgBox.setText("Couldn't Read " + fileName + "\n\n" + statusText);
    msgBox.exec();
    qDebug().noquote() << "Config file restore couldn't read" << fileName
                       << statusText;
  }
}

void ConfigTab::on_startButton_clicked()
{
  Config config;
  if (GetConfig(config))
  {
    qDebug().noquote() << "Starting tag with config:"
                       << QString::fromStdString(config.DebugString());
    if (!tag->Start(config))
    {
      std::string message = tag->DebugMessage();
      msgBox.setText(message.empty() ? QStringLiteral("Start Failed")
                                     : QString::fromStdString(message));
      msgBox.exec();
    }
  } else {
    qDebug() << "on_startButton_clicked failed to get config";
  }
}

void ConfigTab::on_readButton_clicked()
{
  Config configin;
  if (tag->GetConfig(configin)) {
    //qDebug()<< QString(configin.DebugString());
    SetConfig(configin);
  }
  else {
    msgBox.setText("Tag config read failed");
    msgBox.exec();
  }
  qDebug()<< "readButton clicked!";
}
