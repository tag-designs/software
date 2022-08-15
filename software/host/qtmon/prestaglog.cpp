#include <QWidget>
#include <QGroupBox>
#include <QSpinBox>

#include <QDebug>
#include <QLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QAbstractButton>

#include "pbenumgroup.h"
#include "tag.pb.h"
#include "tagclass.h"
#include "prestaglog.h"

PresTagLogTab::PresTagLogTab(QWidget *parent) : QWidget(parent)
{
    vbox_ = new QVBoxLayout();
    setLayout(vbox_);
}

PresTagLogTab::~PresTagLogTab(){};

void PresTagLogTab::Attach(const Config &config)
{
    // Build the interface

    if (config.has_prestag_log())
    {
        spinner_ = new QGroupBox("LPS Data Log");
        QFormLayout *form_ = new QFormLayout;
        period = new QSpinBox();
        period->setSuffix(" sec");
        period->setAlignment(Qt::AlignRight);
        period->setRange(1, 120);
        period->setSingleStep(5);
        form_->addRow("Sample Period", period);
        spinner_->setLayout(form_);
        vbox_->addWidget(spinner_);
        SetConfig(config);
    }
}

void PresTagLogTab::SetConfig(const Config &config)
{
    if (period)
    {
        period->setValue(config.prestag_log().period());
    }
}

void PresTagLogTab::GetConfig(Config &config)
{
    if (period){
        PresTagLogFmt fmt;
        fmt.set_period(period->value());
        //fmt.set_internal(true);
        config.set_allocated_prestag_log(new PresTagLogFmt(fmt));
    }
}
