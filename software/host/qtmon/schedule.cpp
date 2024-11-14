#include <QDate>
#include <QDateTime>
#include <QDebug>
#include "schedule.h"
#include "ui_schedule.h"
#include "hibernate.h"
#include "tag.pb.h"

Schedule::Schedule(QWidget *parent) : QWidget(parent),
                                      ui_(new Ui::Schedule)
{
  ui_->setupUi(this);

  connect(ui_->startButtons, SIGNAL(buttonClicked(int)), this,
          SLOT(on_startGroup_clicked()));
  connect(ui_->runButtons, SIGNAL(buttonClicked(int)), this,
          SLOT(on_runGroup_clicked()));
}

Schedule::~Schedule()
{
  delete ui_;
}

// compute time rounded up to nearest number of seconds

static int64_t roundEpoch(int64_t epoch, int64_t seconds)
{
  epoch += seconds - 1;
  epoch -= epoch % seconds;
  return epoch;
}

static QDateTime roundDateTime(QDateTime dateTime, int64_t seconds)
{
  int64_t epoch = roundEpoch(dateTime.toSecsSinceEpoch(), seconds);
  return QDateTime::fromSecsSinceEpoch(epoch);
}

void Schedule::SetConfig(const Config &config)
{
  QDateTime now = roundDateTime(QDateTime::currentDateTime(), 60);
  QDateTime start = QDateTime::fromSecsSinceEpoch(roundEpoch(config.active_interval().start_epoch(), 60));

  if (config.active_interval().end_epoch() == INT32_MAX)
  {
    ui_->RunIndefinitely->setChecked(true);
    //ui_->EndDateTime->setEnabled(false);
  }
  else
  {
    ui_->RunUntil->setChecked(true);
    //ui_->EndDateTime->setEnabled(true);
  }
  QDateTime end = QDateTime::fromSecsSinceEpoch(config.active_interval().start_epoch());
  ui_->EndDateTime->setDateTime(end);

  // Schedule

  ui_->StartDateTime->setMinimumDateTime(now);

  if (start < now)
  {
    ui_->StartNow->setChecked(true);
    //ui_->StartDateTime->setEnabled(false);
  }
  else
  {
    ui_->StartOn->setChecked(true);
    ui_->StartDateTime->setDateTime(start);
    //ui_->StartDateTime->setEnabled(true);
  }

  if (ui_->RunIndefinitely->isChecked())
  {
    ui_->EndDateTime->setDateTime(ui_->StartDateTime->dateTime());
  }
  // Hibernation -- first we throw away old hibernation
  //                groups

  Hibernate *h = nullptr;
  QLayoutItem *child;
  while ((child = ui_->hibernationGroup->layout()->takeAt(0)) != 0)
  {
    delete child->widget();
    delete child;
  }

  hibernate_list_.clear();

  for (int i = 0; i < config.hibernate_size(); i++)
  {
    h = new Hibernate(i);
    if ((config.hibernate(i).start_epoch() == INT32_MAX) ||
        (config.hibernate(i).start_epoch() == 0))
    {
      h->setStartEpoch(now.currentSecsSinceEpoch());
      h->setEndEpoch(now.currentSecsSinceEpoch());
    }
    else
    {
      h->setStartEpoch(config.hibernate(i).start_epoch());
      h->setEndEpoch(config.hibernate(i).end_epoch());
    }
    hibernate_list_.append(h);
    ui_->hibernationGroup->layout()->addWidget(h);
  }
  if (config.period()) {
    ui_->period->setValue(config.period());
  }
  ui_->periodGroup->setVisible(config.period() != 0);
}

void Schedule::GetConfig(Config &config)
{
  QDateTime now = QDateTime::currentDateTime();
  QDateTime start = ui_->StartDateTime->dateTime();
  QDateTime end = ui_->EndDateTime->dateTime();
  QDateTime maxdt = QDateTime::fromSecsSinceEpoch(INT32_MAX);

  Config_Interval *active = new Config_Interval();
  if (ui_->StartNow->isChecked() || (start < now))
  {
    active->set_start_epoch(0);
  }
  else
  {
    active->set_start_epoch(static_cast<int32_t>(start.toSecsSinceEpoch()));
  }

  if (ui_->RunUntil->isChecked() && (end < maxdt))
  {
    active->set_end_epoch(static_cast<int32_t>(end.toSecsSinceEpoch()));
  }
  else
  {
    active->set_end_epoch(INT32_MAX);
  }

  config.set_allocated_active_interval(active);
  config.clear_hibernate();

  for (int i = 0; i < hibernate_list_.size(); i++)
  {
    Config_Interval *interval = config.add_hibernate();
    interval->set_start_epoch(hibernate_list_.at(i)->StartEpoch());
    interval->set_end_epoch(hibernate_list_.at(i)->EndEpoch());
  }
  if (ui_->periodGroup->isVisible()) {
    config.set_period(ui_->period->value());
  }
  else
  {
    config.set_period(0);
  }
  
}

/*
 *   Start/End Configuration
 */

// Handle changes to start and end times

void Schedule::on_StartDateTime_dateTimeChanged(const QDateTime &dateTime)
{
  QTime time = dateTime.time();
  QDateTime dt = dateTime;
  if (time.minute() != 0) {
        time.setHMS(time.hour() + 1, 0, 0, 0);
        dt.setTime(time);
        ui_->StartDateTime->setDateTime(dt);
    }
  ui_->EndDateTime->setMinimumDateTime(dt);
}

void Schedule::on_EndDateTime_dateTimeChanged(const QDateTime &dateTime)
{
  QTime time = dateTime.time();
  QDateTime dt = dateTime;
  if (time.minute() != 0) {
        time.setHMS(time.hour() + 1, 0, 0, 0);
        dt.setTime(time);
        ui_->EndDateTime->setDateTime(dt);
    }
}

void Schedule::on_runGroup_clicked()
{
  QDateTime now = roundDateTime(QDateTime::currentDateTime(), 60);
  QDateTime maxdt = QDateTime::fromSecsSinceEpoch(INT32_MAX);

  if (ui_->RunIndefinitely->isChecked())
  {
    //ui->StartDateTime->setMaximumDateTime(maxdt);
    ////ui_->EndDateTime->setEnabled(false);
  }

  if (ui_->RunUntil->isChecked())
  {
    if (ui_->EndDateTime->minimumDateTime() < now)
    {
      ui_->EndDateTime->setMinimumDateTime(now);
    }
    //ui_->EndDateTime->setEnabled(true);
  }
}

// Handle changes to End Time

void Schedule::on_startGroup_clicked()
{
  QDateTime now = roundDateTime(QDateTime::currentDateTime(), 60);
  if (ui_->StartNow->isChecked())
  {
    ui_->StartDateTime->setDateTime(now);
    //ui_->StartDateTime->setEnabled(false);
  }

  if (ui_->StartOn->isChecked())
  {
    if (ui_->StartDateTime->dateTime() < now)
    {
      ui_->StartDateTime->setDateTime(now);
    }
    //ui_->StartDateTime->setEnabled(true);
  }
}

/*
 *   Hibernation Configuration
 */

void Schedule::Attach(const Config &config)
{
 // qDebug() << "schedule saw attach !";
  SetConfig(config);
  ui_->EndDateTime->setEnabled(true);
  ui_->StartDateTime->setEnabled(true);
  //this->setEnabled(true);
}
