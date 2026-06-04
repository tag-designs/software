/**
 * @file state_run.c
 * @brief IMUTagBreakout RUNNING-state acquisition and logging logic.
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
#include "debug_log.h"
#include "sensors.h"

#if !defined(CONFIG_HAS_HIBERNATE)
#define CONFIG_HAS_HIBERNATE 1
#endif

static enum Sleep finishRun(State_Event reason)
{
  (void)deinitDataCollection();
  return Finished(T_INIT, reason);
}

static enum Sleep abortRun(State_Event reason)
{
  (void)deinitDataCollection();
  return Aborted(T_INIT, reason);
}

/**
 * @brief Handle the IMUTagBreakout data-acquisition state.
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
    return abortRun(reason);
  }

  if (t == T_INIT)
  {

    disableAllAlarms();
    disableTicker();
    //accelDeinit();

    // reset state variables

    pState->pages = 0;
    pState->cycle_count = 0;
    pState->activity = 0;
    //pState->lastwakeup = timestamp;
    pState->lastwrite = timestamp;
    pState->lastactstart = INT_MAX;

    pState->external_blocks = 0;

    // get voltage, internal temperature

    adcVDD(&vdd100, &temp10);

    pState->vdd100 = vdd100;
    pState->temp10 = temp10;

    if (!initDataCollection()) {
      return abortRun(State_EVENT_UNKNOWN);
    }

    pState->state = TagState_RUNNING;
    recordState(reason);
    debug_log_printf("IMUTag running: collection initialized\r\n");
  }
  else
  {

    // check for completion

    if (sconfig.stop < timestamp)
    {
      return finishRun(State_EVENT_ENDTIM);
    }


    // restore state

    int32_t lastwrite = pState->lastwrite;
    uint32_t cycle_count = pState->cycle_count;

    isActive = palReadLine(LINE_ACCEL_INT);
    if (isActive)
    {
      //  update temperature/voltage estimates 

      adcVDD(&vdd100, &temp10);
      pState->vdd100 = (pState->vdd100 * 3 + vdd100) / 4;
      pState->temp10 = (pState->temp10 * 3 + temp10) / 4;

      /*
       * Bring-up path: drain complete IMU FIFO blocks so the watermark
       * interrupt cadence can be observed on hardware, but do not write the
       * data to external flash yet.
       */
      for (;;) {
        t_DataLog data;
        int16_t env_temp10;

        if (!sampleDataCollection(&data)) {
          break;
        }
        if (latestDataCollectionTemp10(&env_temp10)) {
          pState->temp10 = env_temp10;
        }
        cycle_count++;
        pState->pages++;
      }

      // update the cycle count

      pState->cycle_count = cycle_count;
    }

    //
    // Check for finish condition
    //

    if (sconfig.stop < timestamp)
    {
      return finishRun(State_EVENT_ENDTIM);
    }

    //
    // Check for hibernation
    //     Only hibernate on datalog block boundary.

#if CONFIG_HAS_HIBERNATE
    for (size_t i = 0; i < sizeof(sconfig.hibernate) / sizeof(Config_Interval); i++)
    {
      if ((timestamp >= sconfig.hibernate[i].start_epoch) &&
          (timestamp < sconfig.hibernate[i].end_epoch) &&
          (pState->external_blocks % (sizeof(t_DataLog) / 2) == 0))
      {
        return Hibernating(T_INIT, State_EVENT_STARTHIB);
      }
    }
#endif

    pState->lastactstart = isActive ? timestamp : INT_MAX;
    pState->lastwrite = lastwrite;
    //pState->lastwakeup = timestamp;
  }
  return STOP1;
}
