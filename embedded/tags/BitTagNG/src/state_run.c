#include "hal.h"
#include <limits.h>
#include "app.h"

#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
#include "datalog.h"
#include "sensors.h"

/*
 * Count-Mode Config
 * In this mode, the tag will record activity counts for fixed time intervals
 * instead of presence of activity per second. Multiple counts can be stored
 * per sample, and this is where the timing and storage parameters are set.
 */

const int32_t chunk_period = 15;  // seconds
const int32_t chunk_number = 4;   // chunks in activity write
const int32_t chunk_bits = 4;     // bits per chunk
const int32_t sample_period = 60; // sampling period between writes
/*
   Range now hardwired to 2g
   Sample rate now hardwired to 12.5 hz
   Sleep sample rate 6hz
*/

static enum LOGERR writeCurrentHeader(void)
{
  t_DataHeader dataheader;
  dataheader.epoch = timestamp;
  dataheader.vdd100 = pState->vdd100;
  dataheader.temp10 = pState->temp10;
  return writeDataHeader(&dataheader);
}

static bool logWriteFailed(enum LOGERR err)
{
  switch (err)
  {
  case LOGWRITE_FULL:
  case LOGWRITE_ERROR:
    return true;
  case LOGWRITE_BAT:
    //return Finished(T_INIT, State_EVENT_LOWBATTERY);
  default:
    return false;
  }
}

enum Sleep Running(enum StateTrans t, State_Event reason)
{
  int16_t temp10;
  uint16_t vdd100;
  if (t == T_ERROR)
  {
    return Aborted(T_INIT, reason);
  }

  if (t == T_INIT)
  {
    // initialize the persistent state

    pState->activity = 0;
    pState->lastwakeup = timestamp;
    pState->lastwrite = (timestamp / sample_period) * sample_period;
    pState->lastactstart = INT_MAX;
   
    // Keep the restored external cursor for partially filled pre-header pages.
    const uint32_t max_external_blocks = pState->pages * DATALOG_SAMPLES;
    if (pState->external_blocks > max_external_blocks)
    {
      pState->external_blocks = max_external_blocks;
    }

    adcVDD(&vdd100, &temp10);

    pState->vdd100 = vdd100;
    pState->temp10 = temp10;

    initActivitySensor();

    pState->state = TagState_RUNNING;
    pState->lastwrite = timestamp;
    pState->activity = 0;
    recordState(reason);
    disableAllAlarms();
    disableTicker();
    enableAlarm(0, ALARM_MINUTE);
  }
  else
  {


    uint32_t activity = pState->activity;
    int32_t lastwrite = pState->lastwrite;
    //int32_t lastwakeup = pState->lastwakeup;
    int32_t lastactstart = pState->lastactstart;

    // sample once ! -- also used in pwr to decide wakeup edge

    isActive = palReadLine(LINE_ACCEL_INT);
    

    // a very rare event is a missed RTC wakeup.  In this case the 
    // timestamp may be beyond the current end of period.  Thus we
    // need to "catchup"

    //  Check alarm flags  -- update temperature/voltage estimates

    if (events & (EVT_RTC_ALRAF | EVT_RTC_ALRBF | EVT_RTC_WUTF )) {
        adcVDD(&vdd100, &temp10);
        pState->vdd100 = (pState->vdd100 * 3 + vdd100) / 4;
        pState->temp10 = (pState->temp10 * 3 + temp10) /4;
    }

    // Now we should loop over seconds between lastwakeup and now 
    //   for (uint32_t next = lastwakeup; next <= timestamp; next++) {
    //       -- if next%60 == 0 -- update temperature, voltage
    //          if next%sample_period == 0 -- write out data -- check for error and possible break here.
    //          if next > lastactstart  -- add to activity
    //    }
    //    now check hibernation/stop/etc
    //    update pstate

    if (timestamp > lastwrite + sample_period)
    {
      if (timestamp >= lastwrite + 2 * sample_period)
      {
         // things are really bad
        lastwrite = (timestamp / sample_period) * sample_period;
        if (lastactstart < lastwrite)
        {
          lastactstart = lastwrite;
        }
        activity = 0;
      }
      else
      {
        // make the timestamp equal to start of next sample period
        timestamp = lastwrite + sample_period;
      }
    }

    // now we need to collect all bits
    // lastactstart == INT_MAX if the tag wasn't active at the last wakeup
    // we don't include the current time (this might be the start of the next sample period)

    for (int i = lastactstart; i < timestamp; i++)
    {
      // figure out which chunk needs to be updated
      int index = ((i / chunk_period) % chunk_number) * chunk_bits;
      activity += (((uint32_t)1) << index);
    }


     // Write out activity (assuming timestamps are correct)

    if (events & EVT_RTC_ALRAF)
    {
      enum LOGERR err = LOGWRITE_OK;
      const bool header_exists = pState->pages > 0;

      if (header_exists)
      {
        uint16_t tmp16 = (uint16_t)activity;
        err = writeDataLog(&tmp16, 1);
        if (logWriteFailed(err))
          return Finished(T_INIT, State_EVENT_INTERNALFULL);
      }

      /*
       * Headers precede activity data. If no header exists yet, this opens the
       * first page. After a data write fills a page, this opens the next one.
       */
      if ((pState->pages * DATALOG_SAMPLES) == pState->external_blocks)
      {
        err = writeCurrentHeader();
        if (logWriteFailed(err))
          return Finished(T_INIT, State_EVENT_INTERNALFULL);
      }

      // update activity status
      lastwrite = timestamp;
      activity = 0;
    }

    // check for completion

    if (sconfig.stop < timestamp)
    {
      return Finished(T_INIT, State_EVENT_ENDTIM);
    }

    //
    // Check for hibernation
    //     Only hibernate on datalog block boundary.

    for (size_t i = 0; i < sizeof(sconfig.hibernate) / sizeof(Config_Interval); i++)
    {
      if ((timestamp >= sconfig.hibernate[i].start_epoch) &&
          (timestamp < sconfig.hibernate[i].end_epoch) &&
          ((pState->external_blocks % DATALOG_SAMPLES) == 0))
      {
        return Hibernating(T_INIT, State_EVENT_STARTHIB);
      }
    }


    // update state
    //     we've "collected" all the bits since the lastactstart up to, but
    //     not including the current timestamp

    pState->lastactstart = isActive ? timestamp : INT_MAX;
    pState->activity = activity;
    pState->lastwrite = lastwrite;
    pState->lastwakeup = timestamp;
  }
  return SHUTDOWN;
}
