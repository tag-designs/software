//#include <QDate>
#include <QDateTime>
#include <QDebug>
//#include <QFile>
//#include <QFileDialog>
#include <QMessageBox>
// #include <QProgressDialog>
// #include <QGroupBox>
// #include <QTextEdit>
// #include <QTime>
// #include <QTimer>
// #include <QtWidgets/QSpacerItem>
// #include <QtWidgets/QSizePolicy>
// #include <ctime>
// #include <fstream>
#include <google/protobuf/util/json_util.h>
#include <streambuf>

#include "hibernate.h"
#include "configtab.h"
#include "adxl362config.h"
//#include "dataconfig.h"
#include "bittaglog.h"
#include "mainwindow.h"

ConfigTab::ConfigTab(QWidget *p) : QTabWidget(p)  
{

  msgBox.setWindowTitle("Error");
  msgBox.setStandardButtons(QMessageBox::Ok);

  // Add Schedule Tab

  QVBoxLayout *layout = new QVBoxLayout(&scheduleTab);
  layout->addWidget(&schedule);
  layout->addWidget(&btlog);
  layout->addStretch(1);
  int index = addTab(&scheduleTab,"Schedule");
  setTabToolTip(index,"Configure Tag Schedule");

  // Sensor Tab

  layout = new QVBoxLayout(&sensorTab);
  layout->addWidget(&adxl);
  layout->addStretch(1);
  index = addTab(&sensorTab,"Sensors");
  setTabToolTip(index,"Configure Sensors");
  StateUpdate(STATE_UNSPECIFIED);
}

ConfigTab::~ConfigTab(){}

bool ConfigTab::isActive(){
  return active;
}

bool ConfigTab::Attach(Tag &t)
{
  tag = &t;

  if (schedule.Attach(t) && btlog.Attach(t) && adxl.Attach(t)) {
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
  setEnabled(false);
  setVisible(false);
  active = false;
}

void ConfigTab::StateUpdate(TagState state)
{
  //if (old_state_ != state)
  {
    if (state == IDLE ) {
      if (schedule.isActive())
        schedule.setEnabled(true);
      if (btlog.isActive())
        btlog.setEnabled(true);
      if (adxl.isActive())
        adxl.setEnabled(true);

    } else {
      schedule.setEnabled(false);
      btlog.setEnabled(false);
      adxl.setEnabled(false);
    }
  }
  //old_state_ = state;
}

/*****************************************************
 *                Configuration TAB                  *
 ****************************************************/

bool ConfigTab::GetConfig(Config &config)
{

  config.set_tag_type(tag_type_);
  
  // Get configuration from children

  if (schedule.GetConfig(config) &&
      btlog.GetConfig(config) &&
      adxl.GetConfig(config))
  {
    return true;
  } else {
    return false;
  }

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
  tag_type_ = new_config.tag_type();
  // we need to sanity check the new and old configs !
  // if any of these return false, this should return false
  if (schedule.SetConfig(new_config) &&
      btlog.SetConfig(new_config) &&
      adxl.SetConfig(new_config))
  {
    active = true;
    setVisible(true);
  } else {
    setVisible(false);
    setEnabled(false);
    active = false;
    qDebug() << "SetConfig failed\n";
  }
  return active;
}

