/**
 * @file    bittaglogpanel.cpp
 * @brief   The BitTag internal log format panel.
 */

#include "bittaglogpanel.h"

#include <QAbstractButton>
#include <QDebug>
#include <QMessageBox>
#include <QVBoxLayout>

#include "pbenumgroup.h"

/// Log format buttons, ids from BitTagLogFmt (BITTAG_UNSPECIFIED has none).
static const std::vector<PBEnumGroup::binfo> bittag_internal_log_buttons{
    {BITTAG_BITPERSEC, "Activity Bit Per Second", nullptr},
    {BITTAG_BITSPERMIN, "Activity Count Per Minute", nullptr},
    {BITTAG_BITSPERFOURMIN, "Activity Count Per Four Minutes", nullptr},
    {BITTAG_BITSPERFIVEMIN, "Activity Count Per Five Minutes", nullptr}};

BitTagLogPanel::BitTagLogPanel(QWidget *parent) : ConfigPanel(parent)
{
  auto *vbox = new QVBoxLayout();
  setLayout(vbox);
  log_ = new PBEnumGroup("BitTag Internal Log", bittag_internal_log_buttons);
  vbox->addWidget(log_);
  log_->setToolTip("Set format for BitTag log");
}

std::vector<std::string_view> BitTagLogPanel::paths() const
{
  return {"bittag_log"};
}

bool BitTagLogPanel::load(const Config &config)
{
  if (config.bittag_log() != BITTAG_UNSPECIFIED)
  {
    if (QAbstractButton *btn = log_->button((int)config.bittag_log()))
    {
      btn->setChecked(true);
      return true;
    }
  }
  else if (config.tag_type() == TagType::BITTAG)
  {
    QMessageBox msgBox;
    QString message = QString("Configuring BitTag but missing log specification");
    msgBox.setText(message);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();
    qInfo() << message;
  }
  return false;
}

bool BitTagLogPanel::store(Config &config) const
{
  int id = log_->checkedId();
  if (!BitTagLogFmt_IsValid(id))
    return false;
  config.set_bittag_log((BitTagLogFmt)id);
  return true;
}
