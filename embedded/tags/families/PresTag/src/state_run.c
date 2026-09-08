/**
 * @file state_run.c
 * @brief PresTag RUNNING-state pressure acquisition and logging logic.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"
#include <limits.h>
#include "app.h"

#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
#include "datalog.h"
#include "devices.h"
#include "lps27hhw.h"
#include "phase_probe.h"

#ifndef PRESTAG_RUNNING_LONG_SLEEP_MODE
#define PRESTAG_RUNNING_LONG_SLEEP_MODE STANDBY
#endif

/**
 * @brief Handle the PresTag data-acquisition state.
 *
 * @param[in] t State transition phase.
 * @param[in] reason Reason for entering or continuing the state.
 * @return Requested low-power mode after the state handler completes.
 */
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
    // initialize the persistent state
    pState->state = TagState_RUNNING;
    recordState(reason);
    // make sure we're pointing to the next data block in case
    // this is a recovery action -- round up in the case of partial blocks
    // written.
    //
    // The unit is DATALOG_SAMPLES, not sizeof(t_DataLog)/2. external_blocks
    // counts 4-byte samples (writeDataLog uses external_blocks * 4, and
    // restoreLog sets pages * DATALOG_SAMPLES), so one page is DATALOG_SAMPLES
    // of it; sizeof(t_DataLog)/2 is that page measured in 16-bit words, which
    // is twice as many. Download pairs header vddHeader[i] with the page at
    // i * sizeof(t_DataLog), so the invariant this must preserve is
    // external_blocks == pages * DATALOG_SAMPLES. Rounding to twice the page
    // advanced the cursor a whole page without advancing pages, which
    // desynchronised every later block from its header for the rest of the run.
    int remainder = pState->external_blocks % DATALOG_SAMPLES;
    if (remainder)
      pState->external_blocks = pState->external_blocks + DATALOG_SAMPLES - remainder;
      // need to recover internal block start
    adcVDD(&vdd100, &temp10);

    pState->vdd100 = vdd100;
    pState->temp10 = vdd100;//temp10;

    // Start the interval timer
    disableAllAlarms();
    disableTicker();
    enableTicker(sconfig.lps_period > 0 ? sconfig.lps_period : 1);
  }
  else
  {

    // check for completion

    if (sconfig.stop < timestamp)
    {
      return Finished(T_INIT, State_EVENT_ENDTIM);
    }

    //
    // Check for hibernation
    //     Only hibernate on datalog block boundary, which is DATALOG_SAMPLES
    //     of external_blocks -- see the unit note in the T_INIT branch above.

    for (size_t i = 0; i < sizeof(sconfig.hibernate) / sizeof(Config_Interval); i++)
    {
      if ((timestamp >= sconfig.hibernate[i].start_epoch) &&
          (timestamp < sconfig.hibernate[i].end_epoch) &&
          (pState->external_blocks % DATALOG_SAMPLES == 0))
      {
        return Hibernating(T_INIT, State_EVENT_STARTHIB);
      }
    }

    // update temperature/voltage

    adcVDD(&vdd100, &temp10);
    tagPhaseProbeAdcDone();
    //pState->temp10 = (pState->temp10 * 3 + temp10) / 4;
    pState->vdd100 = (pState->vdd100 * 3 + vdd100)/ 4;

    // check for battery exhausted

    /*
    if ((pState->vdd100) < 200)
    {
      //return Finished(T_INIT, State_EVENT_LOWBATTERY);
    }
    */

    // wakeup timer event ?

    if (events & EVT_RTC_WUTF)
    {
      enum LOGERR err = LOGWRITE_OK;

      struct{
        int16_t pressure;
        int16_t temperature;
      } datablock;

      tagPhaseProbeBegin();
      tagPhaseProbeMark(0);   /* sample tick: entering the sample path */
      lps27GetPressureTemp(TAG_PRESSURE_DEVICE, &datablock.pressure,
                           &datablock.temperature);
      tagPhaseProbeMark(5);   /* pressure read complete, sensor powered off */
      t_DataHeader dataheader;

      if ((pState->external_blocks % (DATALOG_SAMPLES)) == (DATALOG_SAMPLES/2))
      {
        pState->temp10 = pState->vdd100;
      }

      if ((pState->external_blocks % (DATALOG_SAMPLES)) == 0)
      {
        dataheader.epoch = timestamp;
        dataheader.vdd100[0] = pState->vdd100;
        dataheader.vdd100[1] = pState->temp10;
        err = writeDataHeader(&dataheader);
        stopMilliseconds(2);
       
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
      }

      // write data 

      tagPhaseProbeMark(6);   /* about to write the sample to external flash */
      err = writeDataLog((uint16_t *)&datablock.pressure, 2);
      tagPhaseProbeMark(11);  /* external flash write returned */
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
      tagPhaseProbeMark(14);  /* sample bookkeeping done, leaving WUTF branch */

      // check error return
      /*
      switch (err)
      {
      case LOGWRITE_FULL:
      case LOGWRITE_ERROR:
        return Finished(T_INIT, State_EVENT_INTERNALFULL);
      default:
        break;
      }
      */  
    }
  }
  if (sconfig.lps_period < 10)
    return STOP2;
  else
    return PRESTAG_RUNNING_LONG_SLEEP_MODE;
}
