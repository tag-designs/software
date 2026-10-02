/**
 * @file    session_facts.c
 * @brief   Capture and validation of the session facts kept with the stored
 *          configuration; see session_facts.h.
 */

#include "session_facts.h"

#include "rtc_api.h"

#include <string.h>

/*
 * Session facts, at the stored config's session_facts offset.
 * The host rebuilds a download from an SWD capture by reading these at
 * these offsets (host/libraries/tagcore/recovery/capturesource.cc). Moving a
 * member is a layout change: bump the region's layout_version in the tag
 * identity record and teach that decoder the new layout. The host refuses a
 * layout version it does not know, but cannot see a change made without one.
 */
_Static_assert(offsetof(t_sessionFacts, version) == 0, "session facts layout: see capturesource.cc");
_Static_assert(offsetof(t_sessionFacts, flags) == 6, "session facts layout: see capturesource.cc");
_Static_assert(offsetof(t_sessionFacts, rtc_offset_ppm) == 8, "session facts layout: see capturesource.cc");
_Static_assert(TAG_SESSION_FACTS_VERSION == 1U, "session facts version: see capturesource.cc");
_Static_assert(TAG_SESSION_FLAG_RTC_OFFSET_VALID == 1U, "session facts flags: see capturesource.cc");


/* Contract documented in session_facts.h. */
void tagSessionFactsCapture(t_sessionFacts *facts)
{
  memset(facts, 0, sizeof(*facts));
  facts->version = TAG_SESSION_FACTS_VERSION;
  /*
   * Read the offset now rather than trusting the cache. The cache is RAM, and
   * is filled only by tagRtcInit(), which runs on a power-on boot and on a
   * set_rtc request. A start normally arrives in a later monitor session than
   * the clock was set in, on a boot that did neither, so the cache would read
   * as never filled and the session would record no offset.
   */
  (void)tagRtcRefreshClockCorrection();
  if (tagRtcClockCorrectionValid())
  {
    facts->flags |= TAG_SESSION_FLAG_RTC_OFFSET_VALID;
    facts->rtc_offset_steps = tagRtcClockCorrectionSteps();
    facts->rtc_offset_ppm = tagRtcClockErrorPpm();
  }
}

/* Contract documented in session_facts.h. */
bool tagSessionFactsRtcOffsetValid(const t_sessionFacts *facts)
{
  return (facts->version == TAG_SESSION_FACTS_VERSION) &&
         ((facts->flags & TAG_SESSION_FLAG_RTC_OFFSET_VALID) != 0U);
}
