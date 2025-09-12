#include "hal.h"
#include <limits.h>
#include "app.h"

#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
#include "datalog.h"
#include "lps.h"
#include "ADXL362.h"

static const int32_t sample_period = 300;
static const int32_t chunk_period = 60;
static const int32_t chunk_number = 5;
static const int32_t chunk_bits = 6;

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
    // recovery code for brownout here?
    return Aborted(T_INIT, reason);
  }

  if (t == T_INIT)
  {

    disableAllAlarms();
    disableTicker();

    // get voltage, internal temperature

    adcVDD(&vdd100, &temp10);

    pState->vdd100 = vdd100;
    pState->temp10 = vdd100;//temp10;

    pState->activity = 0;
    pState->lastwakeup = timestamp;
    pState->lastwrite = timestamp;
    pState->lastactstart = INT_MAX;

    // round up external pages

    pState->external_blocks == pState->pages*DATALOG_SAMPLES;

     // write out a new header

     t_DataHeader dataheader;
     dataheader.epoch = timestamp;
     dataheader.vdd100[0] = pState->vdd100;
     dataheader.vdd100[1] = pState->temp10;
     switch (writeDataHeader(&dataheader)) {
      case LOGWRITE_FULL:
        return Finished(T_INIT, State_EVENT_INTERNALFULL);
      case LOGWRITE_BAT:
        //return Finished(T_INIT, State_EVENT_LOWBATTERY);
      default:
        break;
    }
    

    pState->state = TagState_RUNNING;
    recordState(reason);

    adxl362_init();

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
      enum LOGERR err = LOGWRITE_OK;


      //  update temperature/voltage estimates 

      adcVDD(&vdd100, &temp10);
      pState->vdd100 = (pState->vdd100 * 3 + vdd100) / 4;

      if ((pState->external_blocks % (DATALOG_SAMPLES)) == (DATALOG_SAMPLES/2))
      {
        pState->temp10 = pState->vdd100;
      }

      // write out data
        
      struct{
        uint32_t activity;
        int16_t pressure;
        int16_t temperature;
      } datablock;

      lpsGetPressureTemp(&datablock.pressure, &datablock.temperature);
      //datablock.pressure = SHRT_MIN;
      //datablock.temperature = SHRT_MAX;
      datablock.activity = activity;
      activity = 0;

      // write data 

      err = writeDataLog((uint16_t *)&datablock.activity, sizeof(datablock)/2);
      switch (err)
      {
      case LOGWRITE_FULL:
      case LOGWRITE_ERROR:
        return Finished(T_INIT, State_EVENT_INTERNALFULL);
      case LOGWRITE_BAT:  //redundant?
        //return Finished(T_INIT, State_EVENT_LOWBATTERY);
      default:
        break;
      }

      pState->external_blocks += 1;

      // new header if we've wrapped around

      if ((pState->external_blocks % (DATALOG_SAMPLES)) == 0)
      {

        t_DataHeader dataheader;
        dataheader.epoch = timestamp;
        dataheader.vdd100[0] = pState->vdd100;
        dataheader.vdd100[1] = pState->temp10;
        switch (writeDataHeader(&dataheader)) {
         case LOGWRITE_FULL:
           return Finished(T_INIT, State_EVENT_INTERNALFULL);
         case LOGWRITE_BAT:
           //return Finished(T_INIT, State_EVENT_LOWBATTERY);
         default:
           break;
       }
      }
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