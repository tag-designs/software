/**
 * @file    schedulepanel.cpp
 * @brief   The "Logging Schedule" panel: start, end, hibernation, period and
 *          start delay.
 */

#include "schedulepanel.h"

#include <algorithm>

#include <QTimeZone>

#include "configclock.h"

/**
 * @brief Rounds an epoch up to a multiple of @p seconds.
 */
static int64_t roundEpoch(int64_t epoch, int64_t seconds)
{
  epoch += seconds - 1;
  epoch -= epoch % seconds;
  return epoch;
}

/**
 * @brief Rounds a date/time up to a multiple of @p seconds.
 */
static QDateTime roundDateTime(QDateTime dateTime, int64_t seconds)
{
  int64_t epoch = roundEpoch(dateTime.toSecsSinceEpoch(), seconds);
  return QDateTime::fromSecsSinceEpoch(epoch);
}

SchedulePanel::SchedulePanel(Variant variant, QWidget *parent) : ConfigPanel(parent)
{
  ui.setupUi(this);
  ui.StartDateTime->setTimeZone(QTimeZone::UTC);
  ui.EndDateTime->setTimeZone(QTimeZone::UTC);

  interval_ = variant != Variant::StartDelay;
  period_ = variant == Variant::IntervalWithPeriod;
  start_delay_ = variant == Variant::StartDelay;

  for (int i = 0; i < kHibernateRows; ++i)
  {
    rows_[i] = new Hibernate(i);
    ui.hibernationGroup->layout()->addWidget(rows_[i]);
  }

  ui.startGroup->setVisible(interval_);
  ui.runGroup->setVisible(interval_);
  ui.hibernationGroup->setVisible(interval_);
  ui.periodGroup->setVisible(period_);
  ui.startdelayGroup->setVisible(start_delay_);
}

std::vector<std::string_view> SchedulePanel::paths() const
{
  std::vector<std::string_view> owned;
  if (interval_)
  {
    owned.push_back("active_interval");
    owned.push_back("hibernate");
  }
  if (period_)
    owned.push_back("period");
  if (start_delay_)
    owned.push_back("start_delay");
  return owned;
}

bool SchedulePanel::load(const Config &config)
{
  QDateTime now = roundDateTime(ConfigClock::now(), 60);

  if (interval_)
  {
    const int32_t start_epoch = config.has_active_interval()
                                    ? config.active_interval().start_epoch()
                                    : 0;
    const int32_t end_epoch = config.has_active_interval()
                                  ? config.active_interval().end_epoch()
                                  : INT32_MAX;
    QDateTime start = QDateTime::fromSecsSinceEpoch(roundEpoch(start_epoch, 60));

    if (end_epoch == INT32_MAX)
      ui.RunIndefinitely->setChecked(true);
    else
      ui.RunUntil->setChecked(true);
    QDateTime end = QDateTime::fromSecsSinceEpoch(end_epoch);
    ui.EndDateTime->setDateTime(end);

    ui.StartDateTime->setMinimumDateTime(now);

    if (start < now)
    {
      ui.StartNow->setChecked(true);
      ui.StartDateTime->setDateTime(now);
    }
    else
    {
      ui.StartOn->setChecked(true);
      ui.StartDateTime->setDateTime(start);
    }

    if (ui.RunIndefinitely->isChecked())
      ui.EndDateTime->setDateTime(ui.StartDateTime->dateTime());

    // Hibernation: one row per interval the tag reports, up to the
    // firmware's maximum. An unset interval (0 or INT32_MAX) shows, and is
    // sent, as 0 to 0: 1 January 1970, long past, so it never applies.
    // Showing it as now, as this once did, sent a window at the current time.
    row_count_ = std::min(config.hibernate_size(), kHibernateRows);
    for (int i = 0; i < kHibernateRows; ++i)
    {
      rows_[i]->setVisible(i < row_count_);
      if (i >= row_count_)
        continue;
      if ((config.hibernate(i).start_epoch() == INT32_MAX) ||
          (config.hibernate(i).start_epoch() == 0))
      {
        rows_[i]->setStartEpoch(0);
        rows_[i]->setEndEpoch(0);
      }
      else
      {
        rows_[i]->setStartEpoch(config.hibernate(i).start_epoch());
        rows_[i]->setEndEpoch(config.hibernate(i).end_epoch());
      }
    }
  }

  // The spin box's minimum of 1 stands in for "unset"; PresTag's firmware
  // rejects a period of 0, so a zero is left showing the previous value.
  if (period_ && config.period())
    ui.period->setValue(config.period());

  if (start_delay_)
    ui.startDelay->setValue(config.start_delay() > 0 ? config.start_delay() - 1 : 0);

  return true;
}

bool SchedulePanel::store(Config &config) const
{
  QDateTime now = ConfigClock::now();
  QDateTime start = ui.StartDateTime->dateTime();
  QDateTime end = ui.EndDateTime->dateTime();
  QDateTime maxdt = QDateTime::fromSecsSinceEpoch(INT32_MAX);

  if (interval_)
  {
    Config_Interval *active = config.mutable_active_interval();
    active->Clear();

    if (ui.StartNow->isChecked() || (start < now))
      active->set_start_epoch(0);
    else
      active->set_start_epoch(static_cast<int32_t>(start.toSecsSinceEpoch()));

    if (ui.RunUntil->isChecked() && end.isValid() && (end < maxdt))
      active->set_end_epoch(static_cast<int32_t>(end.toSecsSinceEpoch()));
    else
      active->set_end_epoch(INT32_MAX);

    config.clear_hibernate();
    for (int i = 0; i < row_count_; i++)
    {
      Config_Interval *interval = config.add_hibernate();
      interval->set_start_epoch(rows_[i]->StartEpoch());
      interval->set_end_epoch(rows_[i]->EndEpoch());
    }
  }

  if (period_)
    config.set_period(ui.period->value());

  if (start_delay_)
    config.set_start_delay(ui.startDelay->value() + 1);

  return true;
}

// Start and end controls

void SchedulePanel::on_StartDateTime_dateTimeChanged(const QDateTime &dateTime)
{
  QTime time = dateTime.time();
  QDateTime dt = dateTime;
  if (time.minute() != 0)
  {
    time.setHMS(time.hour() + 1, 0, 0, 0);
    dt.setTime(time);
    ui.StartDateTime->setDateTime(dt);
  }
  ui.EndDateTime->setMinimumDateTime(dt);
}

void SchedulePanel::on_EndDateTime_dateTimeChanged(const QDateTime &dateTime)
{
  QTime time = dateTime.time();
  QDateTime dt = dateTime;
  if (time.minute() != 0)
  {
    time.setHMS(time.hour() + 1, 0, 0, 0);
    dt.setTime(time);
    ui.EndDateTime->setDateTime(dt);
  }
}

void SchedulePanel::on_runGroup_clicked()
{
  QDateTime now = roundDateTime(ConfigClock::now(), 60);

  if (ui.RunUntil->isChecked())
  {
    if (ui.EndDateTime->minimumDateTime() < now)
      ui.EndDateTime->setMinimumDateTime(now);
  }
}

void SchedulePanel::on_startGroup_clicked()
{
  QDateTime now = roundDateTime(ConfigClock::now(), 60);
  if (ui.StartNow->isChecked())
    ui.StartDateTime->setDateTime(now);

  if (ui.StartOn->isChecked())
  {
    if (ui.StartDateTime->dateTime() < now)
      ui.StartDateTime->setDateTime(now);
  }
}
