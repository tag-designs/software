
#include <QDebug>
#include <QGroupBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QLayout>
#include <QSpinBox>
#include "pbenumgroup.h"

#include "tag.pb.h"
//#include "host.pb.h"
#include "tagclass.h"
#include "dataconfig.h"
#include "bittaglog.h"

DataConfig::DataConfig(QWidget *parent) : QWidget(parent) 
{
    vbox_ = new QVBoxLayout;
    setLayout(vbox_);
}

DataConfig::~DataConfig(){
    active = false;
}

void DataConfig::AddConfigItem(QWidget *item, const Config &config)
{
    // helper for adding children to 
    // layout and local list
    configlist_.append(item);
    ((ConfigInterface *) item)->Attach(config);
    vbox_->addWidget(item);
}

bool DataConfig::SetConfig(const Config &config)
{
    // set config for children
    for (int i = 0; i < configlist_.size(); i++)
    {
        ((ConfigInterface *) configlist_.at(i))->SetConfig(config);
    }
    active = true;
    return active;
}

bool DataConfig::GetConfig(Config &config)
{
    // get config from children
    for (int i = 0; i < configlist_.size(); i++)
    {
        ((ConfigInterface *) configlist_.at(i))->GetConfig(config);
    }
    return true;
}

bool DataConfig::Attach(const Config &config)
{
    // create children for any
    // logs that are configured
    if (config.bittag_log() != BITTAG_UNSPECIFIED) {
        AddConfigItem(new BitTagLogTab(), config);
    } 
    vbox_->addStretch(1);
    active = true;
    return active;
}
