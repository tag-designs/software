#include "sensors.h"
#include "adxl362config.h"
#include "tag.pb.h"
#include "tagclass.h"

Sensors::Sensors(QWidget *parent) : QWidget(parent)
{
    scroll.setWidgetResizable(True);
    inner.setLayout(QVBoxLayout);
    scroll.setWidget(inner);
    /*
    vbox_ = new QVBoxLayout();
    setLayout(vbox_);
    */
    // Sensors are added in TagAttach
}

Sensors::~Sensors()
{
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
    // need to reting this -- 
    // so that this is done a per widget basis.  
    // need a better sensor object type which
    // accepts GetConfig/SetConfig messages
    
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
        inner.layout().addwidget(adxlconfig_);
        //vbox_->addWidget(adxlconfig_);
        adxlconfig_->setEnabled(false);
    }
    // Add to layout

    inner.addStretch();
    this->setEnabled(true);
}

// clear the layout 
//     release all sensors

void Sensors::Detach()
{
    this->setEnabled(false);

    QLayoutItem *item;
    while ((item = inner.takeAt(0))) {
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
    delete item;

        /*
        while (vbox_->count() > 0)
        {
            QLayoutItem *item = vbox_->takeAt(0);
            QWidget *widget = vbox_->widget();
            if (widget)
                delete widget;
            delete item;
        }
        */
}

void Sensors::StateUpdate(State_TagState state)
{
    QLayoutItem *item;
    while ((item = inner.takeAt(0))) {
        if (QWidget *widget = item->widget()) {
            widget->setEnabled(state == State_TagState_IDLE);
}