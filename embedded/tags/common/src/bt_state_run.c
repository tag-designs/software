#include "hal.h"
#include <limits.h>
#include "ADXL362.h"
#include "app.h"

#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
extern const Config defaultConfig;

/*
 * Count-Mode Config
 * In this mode, the tag will record activity counts for fixed time intervals
 * instead of presence of activity per second. Multiple counts can be stored
 * per sample, and this is where the timing and storage parameters are set.
 */

typedef struct
{
  int32_t chunk_period; // number of seconds in chunk
  int32_t chunk_number; // number of chunks in word
  int32_t chunk_bits;   // bit width of chunk
} t_chunk;

static const t_chunk chunks[] = {
    [BitTagLogFmt_BITTAG_BITPERSEC] = {1, 60, 1},          // 1 second,   60 chunks, 1 bit
    [BitTagLogFmt_BITTAG_BITSPERMIN] = {60, 10, 6},        // 60 seconds, 10 chunks, 6 bits
    [BitTagLogFmt_BITTAG_BITSPERFOURMIN] = {60 * 4, 8, 8}, // 4 minutes,   8 chunks, 8 bits
    [BitTagLogFmt_BITTAG_BITSPERFIVEMIN] = {60 * 5, 7, 9}  // 5 minutes,   7 chunks, 9 bits
};


static void adxl362_init(void)
{
  // set up ADXL and alarm

  accelSpiOn();
  ADXL362_SetRegisterValue(0, ADXL362_REG_POWER_CTL, 1);

  // set adxl filter;

  ADXL362_SetRegisterValue(sconfig.adxl_filter_range_rate,
                           ADXL362_REG_FILTER_CTL, 1);
  // set adxl activity detection

  ADXL362_SetupActivityDetection(1, sconfig.adxl_act_thresh_cnt, 2);

  // set adxl inactivity detection

  ADXL362_SetupInactivityDetection(1, sconfig.adxl_inact_thresh_cnt, sconfig.adxl_inactive_samples);

  // ADXL362_SetupInactivityDetection(1, ACTIVE_THRESH,INACTIVE_TIME);

  ADXL362_SetRegisterValue(0x3F, ADXL362_REG_ACT_INACT_CTL, 1);

  // interrupt -- caused by AWAKE going active
  ADXL362_SetRegisterValue(ADXL362_INTMAP2_AWAKE, ADXL362_REG_INTMAP2, 1);
  // power
  ADXL362_SetRegisterValue(2 | ADXL362_POWER_CTL_AUTOSLEEP,
                           ADXL362_REG_POWER_CTL, 1);
  accelSpiOff();
}

enum Sleep Running(enum StateTrans t, State_Event reason)
{
  int16_t temp10;
  uint16_t vdd100;

  if (t == T_ERROR)
  {
    return Aborted(T_INIT, reason);
  }

  int32_t sample_period = chunks[sconfig.internal_format].chunk_period *
                          chunks[sconfig.internal_format].chunk_number;

  if (t == T_INIT)
  {
    // initialize the persistent state

    pState->activity = 0;
    pState->lastwakeup = timestamp;
    pState->lastwrite = (timestamp / sample_period) * sample_period;
    pState->lastactstart = INT_MAX;

    // initialize running average of temperature and voltage

    
    adcVDD(&vdd100, &temp10);

    pState->vdd100 = vdd100;
    pState->temp10 = temp10;

    adxl362_init();

    pState->state = TagState_RUNNING;
    recordState(reason);
    enableAlarm(0, ALARM_MINUTE); // turn on minutes alarm
  }
  else
  {

    // restore state

    uint64_t activity = pState->activity;
    int32_t lastwrite = pState->lastwrite;
    int32_t lastwakeup = pState->lastwakeup;
    int32_t lastactstart = pState->lastactstart;

    // sample once ! -- also used in pwr to decide wakeup edge

    isActive = palReadLine(LINE_ACCEL_INT);

    
    // flush out current data
    // this is parameterized by the number of bits used for the various
    // formats

    uint32_t chunk_period = chunks[sconfig.internal_format].chunk_period;
    uint32_t chunk_number = chunks[sconfig.internal_format].chunk_number;
    uint32_t chunk_bits = chunks[sconfig.internal_format].chunk_bits;

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
      activity += (((uint64_t)1) << index);
    }

    // Write out activity (assuming timestamps are correct)

    if (timestamp == lastwrite + sample_period)
    { // data log write returns an error if battery or space is exhausted
      enum LOGERR err = writeDataLog(activity);

      // Go to finish if battery is too low or log is full

      switch (err)
      {
      case LOGWRITE_FULL:
        return Finished(T_INIT, State_EVENT_INTERNALFULL);
      case LOGWRITE_BAT:
        //return Finished(T_INIT, State_EVENT_LOWBATTERY);
      default:
        break;
      }

      // update activity status
      lastwrite = (timestamp / sample_period) * sample_period;
      activity = 0;
    }

    //
    // Check for finish condition
    //

    if (sconfig.stop < timestamp)
    {
      return Finished(T_INIT, State_EVENT_ENDTIM);
    }

    //
    // Check for hibernation
    //

    for (size_t i = 0; i < sizeof(sconfig.hibernate) / sizeof(Config_Interval); i++)
    {
      if ((timestamp >= sconfig.hibernate[i].start_epoch) &&
          (timestamp < sconfig.hibernate[i].end_epoch))
        return Hibernating(T_INIT, State_EVENT_STARTHIB);
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
