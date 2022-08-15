#include "sensors.h"
#include "adxl362config.h"
#include "tag.pb.h"
#include "tagclass.h"

Sensors::Sensors(QWidget *parent) : QWidget(parent)
{

    vbox_ = new QVBoxLayout();
    setLayout(vbox_);

    // Sensors are added in TagAttach
}

Sensors::~Sensors()
{
    delete adxlconfig_;
}

void Sensors::SetConfig(const Config &config)
{
    // should check to see if 
    // new config matches old for adxl362.

    if (config.has_adxl362())
    {
        adxlconfig_->setHidden(false);
        adxlconfig_->setEnabled(true);
        adxlconfig_->SetAdxl362(config.adxl362());
    }
    else
    {
        adxlconfig_->setHidden(true);
    }
}

void Sensors::GetConfig(Config &config)
{
    if (adxlconfig_)
    {
        Adxl362 adxl;
        adxlconfig_->GetAdxl362(adxl);
        config.set_allocated_adxl362(new Adxl362(adxl));
    }
}

void Sensors::Attach(const Config &config)
{
    if (config.has_adxl362())
    {
        adxlconfig_ = new Adxl362Config("Accelerometer");
        vbox_->addWidget(adxlconfig_);
        adxlconfig_->setEnabled(false);
    }
    // Add to layout

    vbox_->addStretch();
    this->setEnabled(true);
}

void Sensors::Detach()
{
    this->setEnabled(false);
    if (vbox_)
    {
        while (vbox_->count() > 0)
        {
            QLayoutItem *item = vbox_->takeAt(0);
            QWidget *widget = vbox_->widget();
            if (widget)
                delete widget;
            delete item;
        }
    }
    delete vbox_;
    delete adxlconfig_;
    adxlconfig_ = nullptr;
}

void Sensors::StateUpdate(State_TagState state)
{
    if (adxlconfig_)
        adxlconfig_->setEnabled(state == State_TagState_IDLE);
}