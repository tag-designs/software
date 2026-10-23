/**
 * @file    hourtime.h
 * @brief   Whole-hour UTC date/time boxes for the configuration schedule.
 *
 * @details Every schedule time (logging start and end, hibernation start and
 *          end) is a whole hour in UTC. The firmware acts on a hibernation's
 *          end only at an alarm that fires on the hour
 *          (`embedded/tags/common/core/src/time.c`, ALARM_HOUR), so an end
 *          between hours would take effect at the next hour; the other times
 *          are whole hours too, so the four boxes behave alike. Shared by
 *          SchedulePanel and Hibernate.
 */

#ifndef HOURTIME_H
#define HOURTIME_H

#include <QDateTime>
#include <QDateTimeEdit>
#include <QTimeZone>

namespace HourTime
{

/// Display format: the minutes are always shown as ":00" and are not editable.
inline constexpr const char *kFormat = "M/d/yy h:00 AP";

/**
 * @brief   Rounds @p dateTime up to the next whole hour (unchanged if it is one).
 * @return  The rounded instant, in UTC.
 */
inline QDateTime ceilToHour(const QDateTime &dateTime)
{
  qint64 epoch = dateTime.toSecsSinceEpoch();
  const qint64 over = ((epoch % 3600) + 3600) % 3600;
  if (over != 0)
    epoch += 3600 - over;
  return QDateTime::fromSecsSinceEpoch(epoch, QTimeZone::UTC);
}

/**
 * @brief   Rounds @p dateTime down to a whole hour (unchanged if it is one).
 * @return  The rounded instant, in UTC.
 */
inline QDateTime floorToHour(const QDateTime &dateTime)
{
  qint64 epoch = dateTime.toSecsSinceEpoch();
  epoch -= ((epoch % 3600) + 3600) % 3600;
  return QDateTime::fromSecsSinceEpoch(epoch, QTimeZone::UTC);
}

/**
 * @brief   Sets a box to UTC with the whole-hour display format.
 * @param[in,out] edit  The date/time box; its tooltip is left to the caller.
 */
inline void setUp(QDateTimeEdit *edit)
{
  edit->setTimeZone(QTimeZone::UTC);
  edit->setDisplayFormat(QString::fromLatin1(kFormat));
}

/**
 * @brief   For a box's dateTimeChanged handler: rounds the box to the hour.
 *
 * @details Times typed in the box are already whole hours, since it has no
 *          minutes to edit; this catches values loaded from a tag or a file.
 *          Setting the box re-emits dateTimeChanged with the rounded value,
 *          so a handler that gets true should return and let that second
 *          call do its work.
 *
 * @param[in,out] edit      The box.
 * @param[in]     dateTime  The value it now holds.
 * @param[in]     down      Round down rather than up. A hibernation start
 *                          rounds down and its end up, so the rounded window
 *                          covers the one asked for and a window within one
 *                          hour does not collapse to nothing.
 * @return  true when @p dateTime was not a whole hour and the box was reset.
 */
inline bool roundBox(QDateTimeEdit *edit, const QDateTime &dateTime, bool down = false)
{
  const QDateTime rounded = down ? floorToHour(dateTime) : ceilToHour(dateTime);
  if (rounded == dateTime)
    return false;
  edit->setDateTime(rounded);
  return true;
}

} // namespace HourTime

#endif // HOURTIME_H
