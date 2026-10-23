/**
 * @file    configclock.h
 * @brief   The wall clock the configuration screen reads, settable for tests.
 *
 * @details The schedule controls read the current time to round the start
 *          time, set the earliest selectable start, and fill unset
 *          hibernation windows. Reading it through one function lets the
 *          configuration probe (`qtmonitor --config-probe`) run a case with a
 *          fixed "now", so its output is the same on every run. Live
 *          qtmonitor never sets the clock and sees the system time.
 *
 * @see     host/applications/qtmon/test/README.md
 */

#ifndef CONFIGCLOCK_H
#define CONFIGCLOCK_H

#include <QDateTime>

namespace ConfigClock
{

/**
 * @brief   Returns the current time as the configuration screen should see it.
 *
 * @return  QDateTime::currentDateTime() (local time) unless a fixed time has
 *          been set with setFixedNow(), in which case that instant, also as
 *          local time.
 */
QDateTime now();

/**
 * @brief   Fixes the time now() returns.
 *
 * @param[in] epoch_seconds   Seconds since the Unix epoch (UTC) to report from
 *                            now() until cleared.
 *
 * @warning For the configuration probe only. Not thread-safe; call from the
 *          GUI thread.
 */
void setFixedNow(qint64 epoch_seconds);

/**
 * @brief   Returns now() to the system clock.
 */
void clearFixedNow();

} // namespace ConfigClock

#endif // CONFIGCLOCK_H
