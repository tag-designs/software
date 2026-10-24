/**
 * @file    schedulepanel.h
 * @brief   The "Logging Schedule" configuration panel, in three fixed variants.
 */

#ifndef SCHEDULEPANEL_H
#define SCHEDULEPANEL_H

#include <QDateTime>

#include "configpanel.h"
#include "hibernate.h"
#include "ui_schedule.h"

/**
 * @class   SchedulePanel
 * @brief   When a tag logs: start and end, hibernation, sampling period, or a
 *          start delay, depending on the variant.
 *
 * @details One class serves three panels. Each is built for a fixed set of
 *          fields, which the variant chooses at construction:
 *
 *          | Variant              | Groups shown                 | Owns |
 *          | -------------------- | ---------------------------- | ---- |
 *          | Interval             | Start, Run, Hibernation      | `active_interval`, `hibernate` |
 *          | IntervalWithPeriod   | Start, Run, Period, Hibernation | the above and `period` |
 *          | StartDelay           | Start Delay                  | `start_delay` |
 *
 *          Hibernation shows up to two rows, the firmware's maximum
 *          (`Config.hibernate max_count:2`). load() shows one row per interval
 *          the tag reports, and store() writes back that many. Each row has
 *          an In use checkbox: an interval starting at 0 or INT32_MAX loads
 *          unchecked, and store() writes an unchecked row as 0 to 0.
 *
 *          `start_delay` is stored as minutes + 1: proto3 cannot send a zero,
 *          and IMUTag's firmware reads the field that way. The control shows
 *          minutes.
 */
class SchedulePanel : public ConfigPanel
{
  Q_OBJECT

public:
  /// Which fields the panel owns; fixed for the panel's lifetime.
  enum class Variant
  {
    Interval,            ///< active_interval and hibernate
    IntervalWithPeriod,  ///< active_interval, hibernate and period (PresTag)
    StartDelay,          ///< start_delay only (IMUTag)
  };

  /**
   * @brief Builds the schedule controls and shows the groups @p variant owns.
   *
   * @details Two hibernation rows are built and stay hidden until load()
   *          shows the ones the tag reports. Start and end times are shown and
   *          entered in UTC.
   *
   * @param[in] variant  Which fields the panel owns; fixed.
   * @param[in] parent   Qt parent.
   */
  explicit SchedulePanel(Variant variant, QWidget *parent = nullptr);

  std::vector<std::string_view> paths() const override;
  bool load(const Config &config) override;
  bool store(Config &config) const override;

private slots:
  // Connected by name by ui.setupUi().
  /// Rounds the logging start up to the hour, and makes it the end's earliest choice.
  void on_StartDateTime_dateTimeChanged(const QDateTime &dateTime);
  /// Rounds the logging end up to the hour.
  void on_EndDateTime_dateTimeChanged(const QDateTime &dateTime);
  /// Immediately sets the start to now; On moves a past start to now.
  void on_startGroup_clicked();
  /// Until: makes now the end's earliest choice.
  void on_runGroup_clicked();

  /**
   * @brief Gives a hibernation row a starting date when the user puts it in
   *        use.
   *
   * @details If @p row was just checked and still shows no date (start at or
   *          before 1 January 1970), its start is set to the logging start
   *          rounded up to the hour, and its end an hour later. A row that
   *          already has a date, such as one unchecked and checked again,
   *          keeps it. Unchecking changes no date.
   *
   * @param[in] row  Index of the row whose checkbox was clicked.
   */
  void hibernateInUseClicked(int row);

private:
  /// Maximum hibernation intervals any firmware accepts.
  static constexpr int kHibernateRows = 2;

  Ui::Schedule ui;             ///< The Start, Run, Hibernation, Period and Start Delay groups.
  bool interval_ = false;      ///< owns active_interval and hibernate
  bool period_ = false;        ///< owns period
  bool start_delay_ = false;   ///< owns start_delay
  Hibernate *rows_[kHibernateRows] = {};  ///< Hibernation rows; the first row_count_ are shown.
  int row_count_ = 0;          ///< hibernation rows the last load() showed
};

#endif // SCHEDULEPANEL_H
