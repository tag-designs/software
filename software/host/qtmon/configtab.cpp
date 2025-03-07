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
#include <fstream>
#include <google/protobuf/util/json_util.h>
#include <streambuf>

#include "hibernate.h"
#include "configtab.h"
#include "adxl362config.h"
//#include "dataconfig.h"
#include "bittaglog.h"
#include "ui_configtab.h"

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
  {
    if (state == IDLE ) {
      if (schedule.isActive())
        schedule.setEnabled(true);
      if (btlog.isActive())
        btlog.setEnabled(true);
      if (adxl.isActive())
        adxl.setEnabled(true);
      ui.configRestoreButton->setEnabled(true);
      ui.startButton->setEnabled(true);
      ui.readButton->setEnabled(true);
    } else {
      schedule.setEnabled(false);
      btlog.setEnabled(false);
      adxl.setEnabled(false);
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

/*
 * configuration file operations
*/

void ConfigTab::on_configSaveButton_clicked()
{
  QFileDialog fd;
  fd.setFileMode(QFileDialog::AnyFile);
  QString fileName = fd.getSaveFileName(this, tr("Save File"),
                                        QDir::homePath() + "/untitled.json",
                                        tr("Protobuf (*.json)"));
  QString errormsg;

  if (fileName.isNull()) {
    return;
  }

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
  QFileDialog fd;
  Config configin;

  QString fileName = fd.getOpenFileName(this, tr("Open File"), QDir::homePath(),
                                        tr("Protobuf (*.json)"));

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
  if (JsonStringToMessage(str, &configin, options2).ok())
  {
    // We should check the configuration that is read
    // against the current configuration to make sure all
    // the fields are implemented
    SetConfig(configin);
  }
  else
  {
    QMessageBox msgBox;
    msgBox.setText("Couldn't Read " + fileName);
    msgBox.exec();
    qDebug() << "Config file restore couldn't read " << fileName;
  }
}

void ConfigTab::on_startButton_clicked()
{
  Config config;
  if (GetConfig(config))
  {
    if (!tag->Start(config))
    {
      msgBox.setText("Start Failed");
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
    qDebug()<< configin.DebugString();
    SetConfig(configin);
  }
  else {
    msgBox.setText("Tag config read failed");
    msgBox.exec();
  }
  qDebug()<< "readButton clicked!";
}
