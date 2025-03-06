#include <QDebug>
#include <QGroupBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QLayout>
#include <QMessageBox>
#include "pbenumgroup.h"

#include "tag.pb.h"
//#include "host.pb.h"
#include "tagclass.h"
#include "bittaglog.h"

static const std::vector<PBEnumGroup::binfo> bittag_internal_log_buttons{
    {BITTAG_BITPERSEC, "Activity Bit Per Second", nullptr},
    {BITTAG_BITSPERMIN, "Activity Count Per Minute", nullptr},
    {BITTAG_BITSPERFOURMIN, "Activity Count Per Four Minutes", nullptr},
    {BITTAG_BITSPERFIVEMIN, "Activity Count Per Five Minutes", nullptr}};

BitTagLogTab::BitTagLogTab(QWidget *parent) : QWidget(parent)
{
    vbox_ = new QVBoxLayout();
    setLayout(vbox_);
    log_ = new PBEnumGroup("BitTag Internal Log", bittag_internal_log_buttons);
    vbox_->addWidget(log_);
    log_->setToolTip("Set format for BitTag log");

}

BitTagLogTab::~BitTagLogTab(){}

bool BitTagLogTab::Attach(Tag &tag)
{
   //return SetConfig(config);
   return true;
}

void BitTagLogTab::Detach() 
{
    setVisible(false);
    active = false;
}

bool BitTagLogTab::SetConfig(const Config &config)
{
    QAbstractButton *btn;
    if (config.bittag_log() != BITTAG_UNSPECIFIED){
        btn = log_->button((int)config.bittag_log());
        if (btn) {
            btn->setChecked(true);
            setVisible(true);
            active = true;
            return true;
        }
    } else {
        if (config.tag_type() == TagType::BITTAG) {
            QMessageBox msgBox;
            QString message = QString("Configuring BitTag but missing log specification");
            msgBox.setText(message);
            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.exec();
            qInfo() << message;
        }
    }
    active = false;
    setVisible(false);
    return false;
}

bool BitTagLogTab::GetConfig(Config &config)
{
    if (active)
    {
        int id = log_->checkedId();
        if (BitTagLogFmt_IsValid(id))
        {
            config.set_bittag_log((BitTagLogFmt)id);
            return true;
        }
    }
    config.set_bittag_log(BITTAG_UNSPECIFIED);
    return false;
}

