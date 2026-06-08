/**
 * @file state_run.c
 * @brief IMUTagBreakout RUNNING-state acquisition and logging logic.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"
#include "app.h"

#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
#include "datalog.h"
#include "debug_log.h"
#include "imutag_log_format.h"
#include "sensors.h"

#if !defined(CONFIG_HAS_HIBERNATE)
#define CONFIG_HAS_HIBERNATE 1
#endif

#define IMU_CLOCK_LOCK_SECONDS 2
#define IMU_LOG_SAMPLES_PER_BLOCK                                           \
  (sizeof(((t_DataLog *)0)->raw_data) / sizeof(((t_DataLog *)0)->raw_data[0]))

static uint32_t discard_blocks;
static uint32_t discarded_blocks;
static int32_t saved_block_epoch;
static uint16_t saved_block_millis;
static bool next_header_resync;

static uint32_t runDiscardBlocks(void)
{
  lsm6dsv16x_trig_odr_t odr;
  lsm6dsv16x_xl_fs_t xl_fs;
  lsm6dsv16x_g_fs_t g_fs;

  if (!get_lsm_config(&odr, &xl_fs, &g_fs)) {
    return 0;
  }

  return ((uint32_t)odr * IMU_CLOCK_LOCK_SECONDS) /
         IMU_LOG_SAMPLES_PER_BLOCK;
}

static bool restartDataCollectionClock(bool mark_resync)
{
  discard_blocks = runDiscardBlocks();
  discarded_blocks = 0;
  saved_block_epoch = timestamp;
  saved_block_millis = (uint16_t)(timestamp_millis & IMUTAG_HEADER_MILLIS_MASK);
  next_header_resync = mark_resync;
  pState->rawtemp = 0;

  if (!initDataCollection()) {
    return false;
  }

  debug_log_printf("IMUTag running: collection resynced, %d s warmup\r\n",
                   IMU_CLOCK_LOCK_SECONDS);
  return true;
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
  if (t == T_ERROR)
  { 
    // recovery code for brownout here?
    return Aborted(T_INIT, reason);
  }

  if (t == T_INIT)
  {

    disableAllAlarms();
    disableTicker();
    //accelDeinit();

    // reset state variables

    pState->pages = 0;
    pState->cycle_count = 0;
    //pState->lastwakeup = timestamp;

    pState->external_blocks = 0;
    if (!restartDataCollectionClock(false)) {
      return Aborted(T_INIT, State_EVENT_UNKNOWN);
    }

    pState->state = TagState_RUNNING;
    recordState(reason);
    debug_log_printf("IMUTag running: collection initialized, %d s warmup\r\n",
                     IMU_CLOCK_LOCK_SECONDS);
  }
  else
  {
    if (reason == State_EVENT_POWERFAIL) {
      (void)deinitDataCollection();
      if (!restartDataCollectionClock(true)) {
        return Aborted(T_INIT, State_EVENT_UNKNOWN);
      }
      pState->state = TagState_RUNNING;
      return STOP1;
    }

    // check for completion

    if (sconfig.stop < timestamp)
    {
      return Finished(T_INIT, State_EVENT_ENDTIM);
    }


    isActive = palReadLine(LINE_ACCEL_INT);
    if (isActive)
    {
      t_DataLog data;
      int16_t env_rawtemp;
      int32_t entry_epoch = timestamp;
      uint16_t entry_millis =
        (uint16_t)(timestamp_millis & IMUTAG_HEADER_MILLIS_MASK);

      if (sampleDataCollection(&data)) {

        if (latestDataCollectionRawTemp(&env_rawtemp)) {
          pState->rawtemp = env_rawtemp;
        }

        if (discarded_blocks < discard_blocks) {
          discarded_blocks++;
        } else {
          if ((pState->external_blocks % DATALOG_SAMPLES) == 0U) {
            t_DataHeader header;
            enum LOGERR err;

            header.epoch = saved_block_epoch;
            header.millis =
              (uint16_t)(saved_block_millis & IMUTAG_HEADER_MILLIS_MASK);
            if (next_header_resync) {
              header.millis |= IMUTAG_HEADER_RESYNC;
            }
            header.rawtemp = (int16_t)pState->rawtemp;

            err = writeDataHeader(&header);
            switch (err) {
            case LOGWRITE_FULL:
            case LOGWRITE_ERROR:
              return Finished(T_INIT, State_EVENT_INTERNALFULL);
            default:
              break;
            }
            if (err == LOGWRITE_OK) {
              pState->cycle_count++;
              next_header_resync = false;
            }
          }

          enum LOGERR err = writeDataLog(&data);
          switch (err) {
          case LOGWRITE_FULL:
          case LOGWRITE_ERROR:
            return Finished(T_INIT, State_EVENT_EXTERNALFULL);
          default:
            break;
          }
          if (err == LOGWRITE_OK) {
            pState->external_blocks++;
          }
        }

        saved_block_epoch = entry_epoch;
        saved_block_millis = entry_millis;
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

#if CONFIG_HAS_HIBERNATE
    for (size_t i = 0; i < sizeof(sconfig.hibernate) / sizeof(Config_Interval); i++)
    {
      if ((timestamp >= sconfig.hibernate[i].start_epoch) &&
          (timestamp < sconfig.hibernate[i].end_epoch) &&
          (pState->external_blocks % DATALOG_SAMPLES == 0))
      {
        return Hibernating(T_INIT, State_EVENT_STARTHIB);
      }
    }
#endif

    //pState->lastwakeup = timestamp;
  }
  return STOP1;
}
