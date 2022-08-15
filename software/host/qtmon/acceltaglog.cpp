#include <QDebug>
#include <QGroupBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QLayout>
#include "pbenumgroup.h"

#include "tag.pb.h"
#include "tag.pb.h"
#include "tagclass.h"
#include "acceltaglog.h"

static const std::vector<PBEnumGroup::binfo> acceltag_external_log_buttons{
    {ACCELTAG_NONE, "None", "No external logging"},
    {ACCELTAG_ADXL362_CONTINUOUS, "Continuous", "Store full bandwidth adxl data"},
    {ACCELTAG_ADXL362_ACTIVE, "Active Only", "Store full bandwidth adxl data when activity is detected"}};

AccelTagLog::AccelTagLog(QWidget *parent) : QWidget(parent)
{
    vbox_ = new QVBoxLayout();
    setLayout(vbox_);
}

AccelTagLog::~AccelTagLog(){}

void AccelTagLog::SetConfig(const Config &config)
{
    // set configuration
    QAbstractButton *btn;
    if (log_)
    {
        btn = log_->button((int)config.acceltag_log());
        if (btn)
            btn->setChecked(true);
    } else {
    // error case to consider -- count on parent 
    // to call attach
    }
}

void AccelTagLog::GetConfig(Config &config)
{
    // get configuration
    if (log_)
    {
        int id = log_->checkedId();
        if (AccelTagLogFmt_IsValid(id))
        {
            config.set_acceltag_log((AccelTagLogFmt)id);
            return;
        }
    }
    config.set_acceltag_log(ACCELTAG_UNSPECIFIED);
}

void AccelTagLog::Attach(const Config &config)
{ 
    // create UI
    if (config.acceltag_log() != ACCELTAG_UNSPECIFIED)
    {
        log_ = new PBEnumGroup("AccelTag External Log", acceltag_external_log_buttons);
        vbox_->addWidget(log_);
        log_->setToolTip("Set format for AccelTag external log");
        SetConfig(config);
    }
}