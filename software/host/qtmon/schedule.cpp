#include <QDate>
#include <QDateTime>
#include <QDebug>
#include "schedule.h"
#include "ui_schedule.h"
#include "hibernate.h"
#include "tag.pb.h"

/**
 * @brief Helper function for rounding epoch up to multiple of seconds
 * 
 * @param epoch   epoch to round
 * @param seconds divisor in seconds
 * @return int64_t 
 */

static int64_t roundEpoch(int64_t epoch, int64_t seconds)
{
  epoch += seconds - 1;
  epoch -= epoch % seconds;
  return epoch;
}

/**
 * @brief Helper funciton to round date/time up to multiple of seconds
 * 
 * @param dateTime time to be rounded
 * @param seconds  divisor in seconds
 * @return QDateTime 
 */

static QDateTime roundDateTime(QDateTime dateTime, int64_t seconds)
{
  int64_t epoch = roundEpoch(dateTime.toSecsSinceEpoch(), seconds);
  return QDateTime::fromSecsSinceEpoch(epoch);
}


Schedule::Schedule(QWidget *parent) : QWidget(parent) {
  ui.setupUi(this);
}

Schedule::~Schedule(){}


bool Schedule::Attach(Tag &tag)
{
  //return SetConfig(config);
  return true;
}

void Schedule::Detach(){
  active = false;
};

bool Schedule::SetConfig(const Config &config)
{
  QDateTime now = roundDateTime(QDateTime::currentDateTime(), 60);
  QDateTime start = QDateTime::fromSecsSinceEpoch(roundEpoch(config.active_interval().start_epoch(), 60));


  if (config.active_interval().end_epoch() == INT32_MAX)
  {
    ui.RunIndefinitely->setChecked(true);
  }
  else
  {
    ui.RunUntil->setChecked(true);
  }
  QDateTime end = QDateTime::fromSecsSinceEpoch(config.active_interval().start_epoch());
  ui.EndDateTime->setDateTime(end);

  // Schedule

  ui.StartDateTime->setMinimumDateTime(now);

  if (start < now)
  {
    ui.StartNow->setChecked(true);
  }
  else
  {
    ui.StartOn->setChecked(true);
    ui.StartDateTime->setDateTime(start);
  }

  if (ui.RunIndefinitely->isChecked())
  {
    ui.EndDateTime->setDateTime(ui.StartDateTime->dateTime());
  }
  // Hibernation -- first we throw away old hibernation
  //                groups

 
  QLayoutItem *child;
  while ((child = ui.hibernationGroup->layout()->takeAt(0)) != 0)
  {
    if (child->widget())
      child->widget()->deleteLater();
    delete child;
  }

  hibernate_list_.clear();

  for (int i = 0; i < config.hibernate_size(); i++)
  {
    Hibernate *h = new Hibernate(i);
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
    ui.hibernationGroup->layout()->addWidget(h);
  }
  if (config.period()) {
    ui.period->setValue(config.period());
  }
  ui.periodGroup->setVisible(config.period() != 0);
  active = true;
  return true;
}

bool Schedule::GetConfig(Config &config)
{
  QDateTime now = QDateTime::currentDateTime();
  QDateTime start = ui.StartDateTime->dateTime();
  QDateTime end = ui.EndDateTime->dateTime();
  QDateTime maxdt = QDateTime::fromSecsSinceEpoch(INT32_MAX);

  Config_Interval *active = new Config_Interval();
  if (ui.StartNow->isChecked() || (start < now))
  {
    active->set_start_epoch(0);
  }
  else
  {
    active->set_start_epoch(static_cast<int32_t>(start.toSecsSinceEpoch()));
  }

  if (ui.RunUntil->isChecked() && (end < maxdt))
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
  if (ui.periodGroup->isVisible()) {
    config.set_period(ui.period->value());
  }
  else
  {
    config.set_period(0);
  }
  return true;
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
        ui.StartDateTime->setDateTime(dt);
    }
  ui.EndDateTime->setMinimumDateTime(dt);
}

void Schedule::on_EndDateTime_dateTimeChanged(const QDateTime &dateTime)
{
  QTime time = dateTime.time();
  QDateTime dt = dateTime;
  if (time.minute() != 0) {
        time.setHMS(time.hour() + 1, 0, 0, 0);
        dt.setTime(time);
        ui.EndDateTime->setDateTime(dt);
    }
}

void Schedule::on_runGroup_clicked()
{
  QDateTime now = roundDateTime(QDateTime::currentDateTime(), 60);
  QDateTime maxdt = QDateTime::fromSecsSinceEpoch(INT32_MAX);

  if (ui.RunIndefinitely->isChecked())
  {
    //ui->StartDateTime->setMaximumDateTime(maxdt);
    ////ui.EndDateTime->setEnabled(false);
  }

  if (ui.RunUntil->isChecked())
  {
    if (ui.EndDateTime->minimumDateTime() < now)
    {
      ui.EndDateTime->setMinimumDateTime(now);
    }
  }
}

// Handle changes to End Time

void Schedule::on_startGroup_clicked()
{
  QDateTime now = roundDateTime(QDateTime::currentDateTime(), 60);
  if (ui.StartNow->isChecked())
  {
    ui.StartDateTime->setDateTime(now);
  }

  if (ui.StartOn->isChecked())
  {
    if (ui.StartDateTime->dateTime() < now)
    {
      ui.StartDateTime->setDateTime(now);
    }
  }
}

