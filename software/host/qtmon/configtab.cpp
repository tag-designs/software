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
#include <ctime>
#include <fstream>
#include <google/protobuf/util/json_util.h>
#include <streambuf>

#include "hibernate.h"
#include "configtab.h"
#include "adxl362config.h"
#include "dataconfig.h"
#include "mainwindow.h"

ConfigTab::ConfigTab(QWidget *p) : QWidget(p)
{

  // Create Layout and tab components

  QVBoxLayout *vbox = new QVBoxLayout;
  tabwidget_ = new QTabWidget;
  vbox->addWidget(tabwidget_);

  // Create Button Groups

  QGroupBox *configFileBox = new QGroupBox("Config File");
  QHBoxLayout *grp1 = new QHBoxLayout();

  // Save button
  savebtn_ = new QPushButton("save");
  savebtn_->setToolTip("Save configuration to a file");
  grp1->addWidget(savebtn_);
  configFileBox->setToolTip("Read/Write Configuration from/to a file");

  // Restore Button
  restorebtn_ = new QPushButton("restore");
  restorebtn_->setToolTip("Restore configuration from a file");
  grp1->addWidget(restorebtn_);
  configFileBox->setLayout(grp1);

  // Repeat for Tag config/start
  
  QGroupBox *configTagBox = new QGroupBox("Config Tag");
  QHBoxLayout *grp2 = new QHBoxLayout();

  startbtn_ = new QPushButton("start");
  startbtn_->setToolTip("Start tag with programmed configuration");
  readbtn_ = new QPushButton("read");
  readbtn_->setToolTip("Read configuration from tag");

  grp2->addWidget(readbtn_);
  grp2->addWidget(startbtn_);
  configTagBox->setToolTip("Tag Configuration Read/Write(Start)");

  configTagBox->setLayout(grp2);

  QHBoxLayout *buttons = new QHBoxLayout();
  buttons->addWidget(configFileBox);
  buttons->addStretch(3);
  buttons->addWidget(configTagBox);

  vbox->addLayout(buttons);
  setLayout(vbox);

  // connect buttons

  connect(startbtn_, &QPushButton::clicked, this, &ConfigTab::start_clicked);
  connect(readbtn_, &QPushButton::clicked, this, &ConfigTab::config_restore_clicked);

  connect(savebtn_, &QPushButton::clicked, this, &ConfigTab::on_saveButton_clicked);
  connect(restorebtn_, &QPushButton::clicked, this, &ConfigTab::on_restoreButton_clicked);
}

ConfigTab::~ConfigTab()
{
    Detach();
}

void ConfigTab::AddConfigItem(ConfigInterface *item, 
        const char *title, const char *tip, const Config &config)
{
  int index = tabwidget_->addTab(item->GetWidget(), QString(title));
  tabwidget_->setTabToolTip(index, tip);
  configlist_.append(item);
  item->Attach(config);
}

void ConfigTab::Attach(const Config &config)
{
  // add children

  AddConfigItem(qobject_cast<ConfigInterface *>(new Schedule()), "Schedule", 
        "Configure Tag Schedule", config);
  if (config.bittag_log() != BITTAG_UNSPECIFIED){
  AddConfigItem(qobject_cast<ConfigInterface *>(new DataConfig()), "Data", 
        "Configure Tag Data Logs", config);
  }
  if (config.has_adxl362()) {
    Adxl362 adxl(config.adxl362());
    const char *title = (adxl.accel_type() == Adxl362_AdxlType_AdxlType_367) ? "Adxl367" : "Adxl362";
    AddConfigItem(qobject_cast<ConfigInterface *>(new Adxl362Config()), title, 
         "Configure Accelerometer", config);
  }

  // enable tabs

  tag_type_ = config.tag_type();
  tabwidget_->setEnabled(true);
}

void ConfigTab::Detach()
{
  // Delete Children

  tabwidget_->clear();
  while (!configlist_.isEmpty()) {
    delete configlist_.takeFirst();
  }

  tabwidget_->setEnabled(false);
}

void ConfigTab::StateUpdate(TagState state)
{
  if (old_state_ != state)
  {
    if (state == IDLE)
    {
      startbtn_->setEnabled(true);
      restorebtn_->setEnabled(true);
      savebtn_->setEnabled(true);
      readbtn_->setEnabled(true);
    }
    else
    {
      startbtn_->setEnabled(false);
      readbtn_->setEnabled(false);
    }
  }
  old_state_ = state;
  
  for (int i = 0; i < configlist_.size(); i++)
  {
     configlist_.at(i)->GetWidget()->setEnabled(state == IDLE);
  }
}

/*****************************************************
 *                Configuration TAB                  *
 ****************************************************/

bool ConfigTab::GetConfig(Config &config)
{

  config.set_tag_type(tag_type_);
  // Get configuration from children

  for (int i = 0; i < configlist_.size(); i++)
  {
    configlist_.at(i)->GetConfig(config);
  }

  // Sanity check

  int64_t end = config.active_interval().end_epoch();

  if (end < QDateTime::currentSecsSinceEpoch())
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Error");
    msgBox.setText("Configuration Error: end time < current time");
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();
    return false;
  }
  else
  {
    return true;
  }
}

void ConfigTab::SetConfig(const Config &new_config)
{
  tag_type_ = new_config.tag_type();
  // we need to sanity check the new and old configs !
  for (int i = 0; i < configlist_.size(); i++)
  {
    configlist_.at(i)->SetConfig(new_config);
  }
}

// Save config to file

void ConfigTab::on_saveButton_clicked()
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
    options.always_print_primitive_fields = true;
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
  }
}

// Restore config from file

void ConfigTab::on_restoreButton_clicked()
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
  }
}
