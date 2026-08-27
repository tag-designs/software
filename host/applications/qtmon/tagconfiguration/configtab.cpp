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
#include "configfieldvisibility.h"
#include "ui_configtab.h"
#include "qtfiledialog.h"

namespace
{

template <typename Options>
auto setAlwaysPrintDefaultFields(Options &options, bool value, int)
    -> decltype(options.always_print_fields_with_no_presence = value, void())
{
  options.always_print_fields_with_no_presence = value;
}

template <typename Options>
auto setAlwaysPrintDefaultFields(Options &options, bool value, long)
    -> decltype(options.always_print_primitive_fields = value, void())
{
  options.always_print_primitive_fields = value;
}

} // namespace

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
  // SetConfig() reveals active sensor modules; before then, keep the default
  // sensor widgets out of the tab widget's startup size hint.
  adxl.setVisible(false);
  lsm.setVisible(false);
  UpdateSensorTabVisibility();
  //index = addTab(&sensorTab,"Sensors");
  //setTabToolTip(index,"Configure Sensors");
  //StateUpdate(STATE_UNSPECIFIED);
}

ConfigTab::~ConfigTab(){}

bool ConfigTab::isActive(){
  return active;
}

/**
 * @brief Reports whether any sensor configuration module is active.
 *
 * @return true if the current configuration exposes ADXL362 or LSM6 controls;
 *         false for tags whose Sensors tab would otherwise be empty.
 */
bool ConfigTab::HasSensorConfiguration()
{
  return adxl.isActive() || lsm.isActive();
}

/**
 * @brief Selects the Schedule sub-tab for deterministic screenshot capture.
 */
void ConfigTab::ShowScheduleTab()
{
  ui.configtabWidget->setCurrentWidget(ui.scheduleTab);
}

/**
 * @brief Selects the Sensors sub-tab for deterministic screenshot capture.
 */
void ConfigTab::ShowSensorTab()
{
  if (HasSensorConfiguration()) {
    ui.configtabWidget->setCurrentWidget(ui.sensorTab);
  } else {
    ui.configtabWidget->setCurrentWidget(ui.scheduleTab);
  }
}

/**
 * @brief Enables or disables display-only fixture behavior.
 *
 * @details In fixture mode, StateUpdate() still enables controls according to
 *          tag state so screenshots look like live qtmonitor, but Start and
 *          Read return before using the live Tag pointer.
 *
 * @param[in] enabled   true when the tab is backed by fixture data.
 */
void ConfigTab::SetFixtureMode(bool enabled)
{
  fixture_mode_ = enabled;
}

/**
 * @brief Shows the Sensors sub-tab only when sensor controls are active.
 */
void ConfigTab::UpdateSensorTabVisibility()
{
  const int sensor_index = ui.configtabWidget->indexOf(ui.sensorTab);
  if (sensor_index < 0) {
    return;
  }

  const bool has_sensor_controls = HasSensorConfiguration();
  ui.configtabWidget->setTabVisible(sensor_index, has_sensor_controls);
  ui.configtabWidget->setTabEnabled(sensor_index, has_sensor_controls);
  if (!has_sensor_controls &&
      ui.configtabWidget->currentWidget() == ui.sensorTab) {
    ui.configtabWidget->setCurrentWidget(ui.scheduleTab);
  }
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
  old_state_ = state;
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
  const ConfigFieldVisibility &visibility = configFieldVisibilityForTag(tag_type_);
  // we need to sanity check the new and old configs !
  // if any of these return false, this should return false
  if (schedule.SetConfig(new_config, visibility) &&
      btlog.SetConfig(new_config, visibility) &&
      adxl.SetConfig(new_config, visibility) &&
      lsm.SetConfig(new_config, visibility))
  {
    UpdateSensorTabVisibility();
    active = true;
    setVisible(true);
    setEnabled(true);
  } else {
    UpdateSensorTabVisibility();
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
    setAlwaysPrintDefaultFields(options, true, 0);
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
  if (fixture_mode_) {
    qInfo() << "Ignoring Start in qtmonitor fixture display mode";
    return;
  }

  Config config;
  if (GetConfig(config))
  {
    qDebug().noquote() << "Starting tag with config:"
                       << QString::fromStdString(config.DebugString());
    if (!tag->Start(config))
    {
      std::string message = tag->DebugMessage();
      Status status;
      if (tag->GetStatus(status))
      {
        old_state_ = status.state();
      }
      QMessageBox startFailedBox;
      startFailedBox.setWindowTitle("Error");
      startFailedBox.setIcon(QMessageBox::Warning);
      startFailedBox.setText("Start Failed");
      startFailedBox.setStandardButtons(QMessageBox::Ok);
      if (!message.empty())
      {
        QString info = QString::fromStdString(message);
        info += "\nLast known state: ";
        info += QString::fromStdString(TagState_Name(old_state_));
        startFailedBox.setInformativeText(info);
        startFailedBox.setDetailedText(QString::fromStdString(message));
      }
      else
      {
        startFailedBox.setInformativeText(
            "Last known state: " +
            QString::fromStdString(TagState_Name(old_state_)));
      }
      startFailedBox.exec();
    }
  } else {
    qDebug() << "on_startButton_clicked failed to get config";
  }
}

void ConfigTab::on_readButton_clicked()
{
  if (fixture_mode_) {
    qInfo() << "Ignoring Read in qtmonitor fixture display mode";
    return;
  }

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
