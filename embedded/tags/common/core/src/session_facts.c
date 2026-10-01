/**
 * @file    session_facts.c
 * @brief   Capture and validation of the session facts kept with the stored
 *          configuration; see session_facts.h.
 */

#include "session_facts.h"

#include "rtc_api.h"

#include <string.h>

/* Contract documented in session_facts.h. */
void tagSessionFactsCapture(t_sessionFacts *facts)
{
  memset(facts, 0, sizeof(*facts));
  facts->version = TAG_SESSION_FACTS_VERSION;
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
