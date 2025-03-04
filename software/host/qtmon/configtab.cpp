#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QGroupBox>
#include <QTextEdit>
#include <QTime>
#include <QTimer>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSizePolicy>
#include <ctime>
#include <fstream>
#include <google/protobuf/util/json_util.h>
#include <streambuf>

#include "hibernate.h"
#include "configtab.h"
#include "adxl362config.h"
#include "dataconfig.h"
#include "bittaglog.h"
#include "mainwindow.h"

ConfigTab::ConfigTab(QWidget *p) : QTabWidget(p)  
{/*
  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->addWidget(&tabs);
  setLayout(layout);
  */

  // build tabs

  // Schedule Tab

  QVBoxLayout *layout = new QVBoxLayout(&scheduleTab);
      
      // add widgets

  layout->addWidget(&schedule);
  layout->addWidget(&btlog);

      // configure tab;
  layout->addStretch(1);
  int index = addTab(&scheduleTab,"Schedule");
  setTabToolTip(index,"Configure Tag Schedule");

  // Sensor Tab

  layout = new QVBoxLayout(&sensorTab);
      // add widgets
  layout->addWidget(&adxl);
        // configure tab
  layout->addStretch(1);
  index = addTab(&sensorTab,"Sensors");
  setTabToolTip(index,"Configure Sensors");

  setEnabled(false);
}

ConfigTab::~ConfigTab()
{
}

bool ConfigTab::isActive(){
  return active;
}

bool ConfigTab::Attach(const Config &config)
{
  SetConfig(config);
  tag_type_ = config.tag_type();
  setEnabled(true);
  setVisible(true);
  if (schedule.Attach(config) && 
      btlog.Attach(config) &&
      adxl.Attach(config))
  {
    setVisible(true);
    setEnabled(true);
    active = true;
  } else {
    setVisible(false);
    setEnabled(false);
    active = false;
  }
  return active;
}

void ConfigTab::Detach()
{
  setEnabled(false);
  schedule.Detach();
  btlog.Detach();
  adxl.Detach();
  setEnabled(false);
  setVisible(false);
  active = false;
}

void ConfigTab::StateUpdate(TagState state)
{

  // should enable/disable here 
  if (old_state_ != state)
  {
    //schedule.StateUpdate(state);
    //bitlog.StateUpdate(state);
  }
  old_state_ = state;
}

/*****************************************************
 *                Configuration TAB                  *
 ****************************************************/

bool ConfigTab::GetConfig(Config &config)
{

  config.set_tag_type(tag_type_);
  
  // Get configuration from children

  schedule.GetConfig(config);
  btlog.GetConfig(config);
  adxl.GetConfig(config);

  // Sanity check -- should put this in the schedule class

  int64_t end = config.active_interval().end_epoch();

  if (end < QDateTime::currentSecsSinceEpoch())
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Error");
    msgBox.setText("Configuration Error: end time < current time");
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();
    return false;
  } else {
    return true;
  }
}

bool ConfigTab::SetConfig(const Config &new_config)
{
  active = false;
  // tag_type_ = new_config.tag_type();
  // we need to sanity check the new and old configs !
  // if any of these return false, this should return false
  if (schedule.SetConfig(new_config) &&
      btlog.SetConfig(new_config) &&
      adxl.SetConfig(new_config))
  {
    active = true;
  }
  return active;
}

