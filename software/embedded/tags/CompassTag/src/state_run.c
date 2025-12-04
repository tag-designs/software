#include "hal.h"
#include <limits.h>
#include "app.h"

#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
#include "datalog.h"
#include "lis2du12.h"
#include "ak09940a.h"
#include "sensors.h"


// activity data 4 bits/15 seconds
// 16 bits/minute

static const uint32_t sample_period = 15;
static const uint32_t chunk_period = 15;
static const uint32_t chunk_number = 4;
static const uint32_t chunk_bits = 4;
static const uint32_t max_cycles = DATALOG_SAMPLES * 4;

enum Sleep Running(enum StateTrans t, State_Event reason)
{
  int16_t temp10;
  uint16_t vdd100;
  if (t == T_ERROR)
  { 
    // recovery code for brownout here?
    return Aborted(T_INIT, reason);
  }

  if (t == T_INIT)
  {

    disableAllAlarms();
    disableTicker();
    accelDeinit();

    // reset state variables

    pState->cycle_count = 0;
    pState->activity = 0;
    pState->lastwakeup = timestamp;
    pState->lastwrite = timestamp;
    pState->lastactstart = INT_MAX;

    // round up external short count

    pState->external_blocks = pState->pages*DATALOG_SAMPLES*sizeof(t_DataLog)/2;

    // get voltage, internal temperature

    adcVDD(&vdd100, &temp10);

    pState->vdd100 = vdd100;
    pState->temp10 = temp10;

     // write out a new header

     t_DataHeader dataheader;
     dataheader.epoch = timestamp;
     dataheader.vdd100 = pState->vdd100;
     dataheader.temp10 = pState->temp10;
     switch (writeDataHeader(&dataheader)) {
      case LOGWRITE_FULL:
        return Finished(T_INIT, State_EVENT_INTERNALFULL);
      case LOGWRITE_BAT:
        //return Finished(T_INIT, State_EVENT_LOWBATTERY);
      default:
        break;
    }

    // start accelerometer

    accelInit(ACCEL_WAKEUP_MODE);
    pState->state = TagState_RUNNING;
    recordState(reason);

    // Start the interval timer

    enableTicker(sample_period);
  }
  else
  {

    // check for completion

    if (sconfig.stop < timestamp)
    {
      return Finished(T_INIT, State_EVENT_ENDTIM);
    }


    // restore state

    uint64_t activity = pState->activity;
    int32_t lastwrite = pState->lastwrite;
    int32_t lastwakeup = pState->lastwakeup;
    int32_t lastactstart = pState->lastactstart;
    uint32_t cycle_count = pState->cycle_count;

    // sample once ! -- also used in pwr to decide wakeup edge

    isActive = palReadLine(LINE_ACCEL_INT);

    // now we need to collect all bits
    // lastactstart == INT_MAX if the tag wasn't active at the last wakeup
    // we don't include the current time (this might be the start of the next sample period)

    for (int i = lastactstart; i < timestamp; i++)
    {
      // figure out which chunk needs to be updated
      int index = ((i / chunk_period) % chunk_number) * chunk_bits;
      activity += (((uint64_t)1) << index);
    }

    // If wakeup timer awoke us


    if (events & EVT_RTC_WUTF)
    {
      //enum LOGERR err = LOGWRITE_OK;


      //  update temperature/voltage estimates 

      adcVDD(&vdd100, &temp10);
      pState->vdd100 = (pState->vdd100 * 3 + vdd100) / 4;
      pState->temp10 = (pState->temp10 * 3 + temp10) / 4;

      // read/write sensors
        
      RawSensorData data;
      bool error;

      // what would we do with a false return???

      error = sensorSample(&data);

      switch (writeDataLog((uint16_t *) &data, sizeof(data)/2))
      {
      case LOGWRITE_FULL:
      case LOGWRITE_ERROR:
        return Finished(T_INIT, State_EVENT_EXTERNALFULL);
      case LOGWRITE_BAT:  //redundant?
        //return Finished(T_INIT, State_EVENT_LOWBATTERY);
      default:
        break;
      }

      pState->external_blocks += sizeof(data)/2;
      cycle_count+= 1;

      // possibly write activty data data

      if ((cycle_count % 4) == 0)
      {
        uint16_t act = activity;

        switch (writeDataLog(&act, 1))
        {
          case LOGWRITE_FULL:
          case LOGWRITE_ERROR:
            return Finished(T_INIT, State_EVENT_EXTERNALFULL);
          case LOGWRITE_BAT:  //redundant?
            //return Finished(T_INIT, State_EVENT_LOWBATTERY);
          default:
            break;
        }
        pState->external_blocks += 1;
        activity = 0;

      }

      // possibly write a new header

      if (cycle_count == max_cycles)
      {
        t_DataHeader dataheader;
        dataheader.epoch = timestamp;
        dataheader.vdd100 = pState->vdd100;
        dataheader.temp10 = pState->temp10;
        switch (writeDataHeader(&dataheader)) {
          case LOGWRITE_FULL:
            return Finished(T_INIT, State_EVENT_INTERNALFULL);
          case LOGWRITE_BAT:
            //return Finished(T_INIT, State_EVENT_LOWBATTERY);
          default:
            break;
        }
        cycle_count = 0;
      }

      // update the cycle count

      pState->cycle_count = cycle_count;
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
    //     Only hibernate on datalog block boundary.

    for (size_t i = 0; i < sizeof(sconfig.hibernate) / sizeof(Config_Interval); i++)
    {
      if ((timestamp >= sconfig.hibernate[i].start_epoch) &&
          (timestamp < sconfig.hibernate[i].end_epoch) &&
          (pState->external_blocks % (sizeof(t_DataLog) / 2) == 0))
      {
        return Hibernating(T_INIT, State_EVENT_STARTHIB);
      }
    }

    pState->lastactstart = isActive ? timestamp : INT_MAX;
    pState->activity = activity;
    pState->lastwrite = lastwrite;
    pState->lastwakeup = timestamp;
  }
  return SHUTDOWN;
}