#include <QDebug>
#include <QGroupBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QLayout>
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
}

BitTagLogTab::~BitTagLogTab(){}

void BitTagLogTab::SetConfig(const Config &config)
{
    QAbstractButton *btn;
    if (log_)
    {
        btn = log_->button((int)config.bittag_log());
        if (btn)
            btn->setChecked(true);
    }
}

void BitTagLogTab::GetConfig(Config &config)
{
    if (log_)
    {
        int id = log_->checkedId();
        if (BitTagLogFmt_IsValid(id))
        {
            config.set_bittag_log((BitTagLogFmt)id);
            return;
        }
    }
    config.set_bittag_log(BITTAG_UNSPECIFIED);
}

void BitTagLogTab::Attach(const Config &config)
{
    if (config.bittag_log() != BITTAG_UNSPECIFIED)
    {
        log_ = new PBEnumGroup("BitTag Internal Log", bittag_internal_log_buttons);
        vbox_->addWidget(log_);
        log_->setToolTip("Set format for BitTag log");
        SetConfig(config);
    }
}