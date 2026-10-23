/**
 * @file    configclock.cpp
 * @brief   System clock with an optional fixed override for the config probe.
 */

#include "configclock.h"

namespace
{
bool fixed_ = false;           ///< True while setFixedNow() is in effect.
qint64 fixed_epoch_s_ = 0;     ///< The fixed instant, seconds since the epoch.
} // namespace

QDateTime ConfigClock::now()
{
  if (fixed_)
    return QDateTime::fromSecsSinceEpoch(fixed_epoch_s_);
  return QDateTime::currentDateTime();
}

void ConfigClock::setFixedNow(qint64 epoch_seconds)
{
  fixed_ = true;
  fixed_epoch_s_ = epoch_seconds;
}

void ConfigClock::clearFixedNow()
{
  fixed_ = false;
}
