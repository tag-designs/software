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

#if !defined(USE_STOP1)
#define USE_STOP1 1
#endif

#define IMU_CLOCK_LOCK_SECONDS 2
#define IMU_LOG_SAMPLES_PER_BLOCK                                           \
  (sizeof(((t_DataLog *)0)->raw_data) / sizeof(((t_DataLog *)0)->raw_data[0]))
#define IMU_MAX_BLOCKS_PER_WAKE 8

/*
 * Maintainer notes
 * ----------------
 *
 * IMUTag logging is more timing-sensitive than the lower-rate tags because the
 * LSM6 FIFO provides an implicit stream of high-rate samples. The log stores
 * one t_DataHeader per DATALOG_SAMPLES external blocks. The header gives an
 * absolute timestamp for the first retained block in that page; individual IMU
 * samples are reconstructed later by sample count and configured ODR.
 *
 * A monitor attach uses connect-under-reset. When that happens during RUNNING,
 * the common state machine re-enters this handler with T_CONT and
 * State_EVENT_POWERFAIL instead of aborting, but the local FIFO phase and
 * software block cache must be treated as lost. The recovery policy here is:
 *
 * - deinitialize/reinitialize the acquisition hardware;
 * - discard a short warmup interval so the IMU clock and FIFO stream settle;
 * - abandon any partial pre-reset page by starting the next header at the first
 *   post-warmup block timestamp;
 * - set IMUTAG_HEADER_RESYNC on that next header so host decoders and SensorViz
 *   can mark the discontinuity.
 *
 * Do not use the subsecond field as a normal per-page timing correction. Only
 * the low ten bits are 1/1024-second subsecond ticks, the upper bits are flags,
 * and the rounded millisecond value can add jitter. Host decoding uses the
 * header only for collection start and explicit resync segment anchors.
 */
static uint32_t discard_blocks;
static uint32_t discarded_blocks;
static int32_t saved_block_epoch;
static uint16_t saved_block_millis;
static uint16_t next_header_flags;

/*
 * Convert the configured IMU ODR into complete t_DataLog blocks to discard
 * after acquisition starts or restarts. This is deliberately block-based so the
 * first retained header points at a real logged block boundary.
 */
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

/*
 * Start or restart the data-collection clock.
 *
 * mark_resync is true only for recovery from a monitor connect-under-reset
 * while already RUNNING. It causes exactly one future internal header to carry
 * IMUTAG_HEADER_RESYNC. Header flags are cleared only after that header write
 * succeeds, so a write failure cannot silently lose the discontinuity marker.
 */
static bool restartDataCollectionClock(bool mark_resync)
{
  discard_blocks = runDiscardBlocks();
  discarded_blocks = 0;
  saved_block_epoch = timestamp;
  saved_block_millis = (uint16_t)(timestamp_millis & IMUTAG_HEADER_MILLIS_MASK);
  next_header_flags = mark_resync ? IMUTAG_HEADER_RESYNC : 0U;
  pState->rawtemp = 0;

  if (!initDataCollection()) {
    return false;
  }

  debug_log_printf("IMUTag running: collection resynced, %d s warmup\r\n",
                   IMU_CLOCK_LOCK_SECONDS);
  return true;
}

typedef enum {
  IMU_BLOCK_NO_DATA,
  IMU_BLOCK_HANDLED,
  IMU_BLOCK_INTERNAL_FULL,
  IMU_BLOCK_EXTERNAL_FULL
} ImuBlockStatus;

static enum LOGERR writeDataLogWithRetry(t_DataLog *data)
{
  enum LOGERR err = writeDataLog(data);

  if (err == LOGWRITE_ERROR) {
    err = writeDataLog(data);
  }
  return err;
}

static ImuBlockStatus sampleAndLogDataBlock(void)
{
  static t_DataLog data;
  int16_t env_rawtemp;
  int32_t entry_epoch = timestamp;
  uint16_t entry_millis =
    (uint16_t)(timestamp_millis & IMUTAG_HEADER_MILLIS_MASK);

  if (!sampleDataCollection(&data)) {
    return IMU_BLOCK_NO_DATA;
  }

  if (latestDataCollectionRawTemp(&env_rawtemp)) {
    pState->rawtemp = env_rawtemp;
  }

  if (discarded_blocks < discard_blocks) {
    discarded_blocks++;
  } else {
    if ((pState->external_blocks % DATALOG_SAMPLES) == 0U) {
      t_DataHeader header;
      enum LOGERR err;

      /*
       * saved_block_* always names the block about to be written under this
       * header. entry_* captures the next candidate boundary and is committed
       * after this block has been sampled.
       */
      header.epoch = saved_block_epoch;
      header.millis =
        (uint16_t)(saved_block_millis & IMUTAG_HEADER_MILLIS_MASK);
      header.millis |= next_header_flags;
      header.rawtemp = (int16_t)pState->rawtemp;

      err = writeDataHeader(&header);
      switch (err) {
      case LOGWRITE_ERROR:
        debug_log_printf(
          "IMUTag running: internal header write error pages=%u ext=%u\r\n",
          (unsigned)pState->pages, (unsigned)pState->external_blocks);
        return IMU_BLOCK_INTERNAL_FULL;
      case LOGWRITE_FULL:
        debug_log_printf(
          "IMUTag running: internal header full pages=%u ext=%u\r\n",
          (unsigned)pState->pages, (unsigned)pState->external_blocks);
        return IMU_BLOCK_INTERNAL_FULL;
      default:
        break;
      }
      if (err == LOGWRITE_OK) {
        pState->cycle_count++;
        next_header_flags = 0U;
      }
    }

    enum LOGERR err = writeDataLogWithRetry(&data);
    switch (err) {
    case LOGWRITE_FULL:
      return IMU_BLOCK_EXTERNAL_FULL;
    case LOGWRITE_ERROR:
      pState->external_blocks++;
      next_header_flags |= IMUTAG_HEADER_RESYNC |
                           IMUTAG_HEADER_RESYNC_STORAGE_SKIP;
      debug_log_printf("IMUTag external log write failed, skipped block %u\r\n",
                       (unsigned)(pState->external_blocks - 1U));
      break;
    default:
      break;
    }
    if (err == LOGWRITE_OK) {
      pState->external_blocks++;
    }
  }

  saved_block_epoch = entry_epoch;
  saved_block_millis = entry_millis;
  return IMU_BLOCK_HANDLED;
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
    debug_log_printf("IMUTag running: abort on T_ERROR reason=%d\r\n", reason);
    return Aborted(T_INIT, reason);
  }

  if (t == T_INIT)
  {

    disableAllAlarms();
    disableTicker();
    //accelDeinit();

    // Fresh run: reset durable log counters and start a new continuous segment.

    pState->pages = 0;
    pState->cycle_count = 0;
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
    pState->run_heartbeat = 0;
    pState->terminal_state = STATE_UNSPECIFIED;
    pState->terminal_reason = State_EVENT_UNSPECIFIED;
    pState->header_status = LOGWRITE_OK;
    pState->header_flasherr = 0;
    pState->header_page = 0;
    pState->header_addr = 0;
    pState->header_retries = 0;
#endif
    //pState->lastwakeup = timestamp;

    pState->external_blocks = 0;
    if (!restartDataCollectionClock(false)) {
      debug_log_printf("IMUTag running: abort, collection init failed\r\n");
      return Aborted(T_INIT, State_EVENT_UNKNOWN);
    }

    pState->state = TagState_RUNNING;
    recordState(reason);
    debug_log_printf("IMUTag running: collection initialized, %d s warmup\r\n",
                     IMU_CLOCK_LOCK_SECONDS);
  }
  else
  {
    enum Sleep sleepmode = USE_STOP1 ? STOP1 : SLEEP;
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
    pState->run_heartbeat++;
#endif

    /*
     * POWERFAIL here is the state machine's recovery token for monitor
     * connect-under-reset during RUNNING. It is not a normal standby wake.
     * Restart the FIFO stream and leave pState counters intact so download
     * continues from the recovered log cursors, with an explicit RESYNC marker
     * before the next retained page.
     */
    if (reason == State_EVENT_POWERFAIL) {
      debug_log_printf("IMUTag running: monitor reset recovery\r\n");
      (void)deinitDataCollection();
      if (!restartDataCollectionClock(true)) {
        debug_log_printf("IMUTag running: abort, collection resync failed\r\n");
        return Aborted(T_INIT, State_EVENT_UNKNOWN);
      }
      pState->state = TagState_RUNNING;
      return USE_STOP1 ? STOP1 : SLEEP;
    }

    // check for completion

    if (sconfig.stop < timestamp)
    {
      debug_log_printf(
        "IMUTag running: stop time reached timestamp=%d stop=%d pages=%u ext=%u\r\n",
        timestamp, sconfig.stop, (unsigned)pState->pages,
        (unsigned)pState->external_blocks);
      return Finished(T_INIT, State_EVENT_ENDTIM);
    }


    for (uint32_t blocks = 0; blocks < IMU_MAX_BLOCKS_PER_WAKE; blocks++)
    {
      switch (sampleAndLogDataBlock()) {
      case IMU_BLOCK_HANDLED:
        break;
      case IMU_BLOCK_INTERNAL_FULL:
        debug_log_printf(
          "IMUTag running: finishing, internal log unavailable pages=%u ext=%u\r\n",
          (unsigned)pState->pages, (unsigned)pState->external_blocks);
        return Finished(T_INIT, State_EVENT_INTERNALFULL);
      case IMU_BLOCK_EXTERNAL_FULL:
        debug_log_printf(
          "IMUTag running: finishing, external log full pages=%u ext=%u\r\n",
          (unsigned)pState->pages, (unsigned)pState->external_blocks);
        return Finished(T_INIT, State_EVENT_EXTERNALFULL);
      case IMU_BLOCK_NO_DATA:
      default:
        blocks = IMU_MAX_BLOCKS_PER_WAKE;
        break;
      }
    }
    isActive = palReadLine(LINE_WKUP1);
    if (isActive) {
      sleepmode = SLEEP;
    }

    //
    // Check for finish condition
    //

    if (sconfig.stop < timestamp)
    {
      debug_log_printf(
        "IMUTag running: stop time reached timestamp=%d stop=%d pages=%u ext=%u\r\n",
        timestamp, sconfig.stop, (unsigned)pState->pages,
        (unsigned)pState->external_blocks);
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
    return sleepmode;
  }
  return STOP1;
}
