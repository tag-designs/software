#include "hal.h"
#include <limits.h>
#include "ADXL367.h"
#include "app.h"

#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
#include "datalog.h"

/*
 * Count-Mode Config
 * In this mode, the tag will record activity counts for fixed time intervals
 * instead of presence of activity per second. Multiple counts can be stored
 * per sample, and this is where the timing and storage parameters are set.
 */

const int32_t chunk_period = 30;  // seconds
const int32_t chunk_number = 4;   // chunks in activity write
const int32_t chunk_bits = 8;     // bits per chunk
const int32_t sample_period = 120; // sampling period between writes
/*
   Range now hardwired to 2g
   Sample rate now hardwired to 12.5 hz
   Sleep sample rate 6hz
*/

#define ADXL_SAMPLE_RATE ADXL367_ODR_12P5HZ
#define UINT16SWAP(x) (((x&0xff)<<8) | ((x>>8)&0xff))
static void adxl367_init(void)
{
  accelSpiOn();
  ADXL367_SoftwareReset();
  stopMilliseconds(true,20);
// Start Measurement
  ADXL367_SetRegisterValue(ADXL367_OP_MEASURE, ADXL367_REG_POWER_CTL, 1);
// interrupt -- caused by AWAKE going active 
  ADXL367_SetRegisterValue(ADXL367_INTMAP1_AWAKE_INT1 , ADXL367_REG_INTMAP1_LWR, 1);
// Timer Control Register -- 6 samples/second
  ADXL367_SetRegisterValue(1<<6, ADXL367_REG_TIMER_CTL,1);
// set inactivity level maximum 4400 = 1.1g for 2G range
  ADXL367_SetRegisterValue(UINT16SWAP(4400<<2),ADXL367_REG_THRESH_INACT_H,2);
// set activity threshold to 1mg -- see p22 ADXL367 data sheet
  ADXL367_SetRegisterValue(UINT16SWAP((4<<2)),ADXL367_REG_THRESH_ACT_H,2);
// set inactive time to 0
  ADXL367_SetRegisterValue(UINT16SWAP(0),ADXL367_REG_TIME_INACT_H,2);
// turn on referenced loop mode
  ADXL367_SetRegisterValue(0x3F, ADXL367_REG_ACT_INACT_CTL, 1);
// set power to MEASUREMENT
  #define POWERCTL (ADXL367_POWER_CTL_WAKEUP | ADXL367_POWER_CTL_AUTOSLEEP | ADXL367_OP_MEASURE)
  ADXL367_SetRegisterValue(POWERCTL, ADXL367_REG_POWER_CTL, 1);
// Check AWAKE bit inactivity.
  while (palReadLine(LINE_ACCEL_INT)) {stopMilliseconds(true,20);};
// set adxl range/rate;  2g, 12.5hz
  ADXL367_SetRegisterValue(ADXL_SAMPLE_RATE,ADXL367_REG_FILTER_CTL, 1);
// set inact timer
  ADXL367_SetRegisterValue(UINT16SWAP(sconfig.adxl_inactive_samples),ADXL367_REG_TIME_INACT_H,2);
// reset active threshold and inactive time
  ADXL367_SetRegisterValue(UINT16SWAP((sconfig.adxl_act_thresh_cnt)<<2),ADXL367_REG_THRESH_ACT_H,2);

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

  if (t == T_INIT)
  {
    // initialize the persistent state

    pState->activity = 0;
    pState->lastwakeup = timestamp;
    pState->lastwrite = (timestamp / sample_period) * sample_period;
    pState->lastactstart = INT_MAX;
   
    // make sure we're pointing to the next data block in case
    // this is a recovery action -- round up in the case of partial blocks
    // written
    int remainder = pState->external_blocks % DATALOG_SAMPLES;
    if (remainder)
      pState->external_blocks = pState->external_blocks + DATALOG_SAMPLES - remainder;
      // need to recover internal block start

    adcVDD(&vdd100, &temp10);

    pState->vdd100 = vdd100;
    pState->temp10 = temp10;

    adxl367_init();

    pState->state = TagState_RUNNING;
    recordState(reason);
    enableAlarm(0, ALARM_MINUTE); // turn on minutes alarm
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

    if (timestamp == lastwrite + sample_period)
    { // data log write returns an error if battery or space is exhausted

    // write a header every N times -- use internal/external count for this
    // header lags by one block
    
      if ((pState->external_blocks%(DATALOG_SAMPLES*2)) == 0) {
        t_DataHeader dataheader;
        dataheader.epoch = timestamp;
        dataheader.vdd100 = pState->vdd100;
        dataheader.temp10 = pState->temp10;
        enum LOGERR err = writeDataHeader(&dataheader);

          switch (err)
          {
          case LOGWRITE_FULL:
            return Finished(T_INIT, State_EVENT_INTERNALFULL);
          case LOGWRITE_BAT:
            //return Finished(T_INIT, State_EVENT_LOWBATTERY);
          default:
            break;
          }
      }

      // write external data (activity)

      uint32_t tmp32 = activity;
      writeDataLog((uint16_t *) &tmp32, 2);
      // update activity status
      lastwrite = (timestamp / sample_period) * sample_period;
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
