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
#define IMU_MAX_PAGE_WORK_PER_WAKE 8

/*
 * Maintainer notes
 * ----------------
 *
 * IMUTag logging is more timing-sensitive than the lower-rate tags because the
 * LSM6 FIFO provides an implicit stream of high-rate samples. The log stores
 * one t_DataHeader per DATALOG_SAMPLES external pages. Each external page also
 * starts with its own timestamp, captured before the first retained superframe
 * sample, so individual IMU samples are reconstructed later by page timestamp,
 * sample count, and configured ODR.
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
static uint32_t discard_pages;
static uint32_t discarded_pages;
static uint16_t next_header_flags;
static bool page_active;
static bool current_page_logging;
static bool current_page_data_header_written;
static bool current_page_header_written;
static t_DataHeader current_page_header;
static uint16_t current_frame_index;
static int32_t next_frame_epoch;
static uint16_t next_frame_millis;

static bool imuWakeActive(void)
{
  return palReadLine(LINE_WKUP1) == PAL_HIGH;
}

static void setNextFrameStartTimestamp(int32_t epoch, uint32_t millis)
{
  next_frame_epoch = epoch;
  next_frame_millis = (uint16_t)(millis & IMUTAG_HEADER_MILLIS_MASK);
}

/*
 * Convert the configured IMU ODR into complete t_DataLog pages to discard
 * after acquisition starts or restarts. This is deliberately page-based so the
 * first retained header points at a real logged page boundary.
 */
static uint32_t runDiscardPages(void)
{
  lsm6dsv16x_trig_odr_t odr;
  lsm6dsv16x_xl_fs_t xl_fs;
  lsm6dsv16x_g_fs_t g_fs;
  uint32_t warmup_samples;

  if (!get_lsm_config(&odr, &xl_fs, &g_fs)) {
    return 0;
  }

  warmup_samples = (uint32_t)odr * IMU_CLOCK_LOCK_SECONDS;
  return (warmup_samples + IMUTAG_IMU_SAMPLES_PER_PAGE - 1U) /
         IMUTAG_IMU_SAMPLES_PER_PAGE;
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
  discard_pages = runDiscardPages();
  discarded_pages = 0;
  next_header_flags = mark_resync ? IMUTAG_HEADER_RESYNC : 0U;
  page_active = false;
  current_page_logging = false;
  current_page_data_header_written = false;
  current_page_header_written = false;
  current_frame_index = 0U;
  setNextFrameStartTimestamp(timestamp, timestamp_millis);
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

static enum LOGERR writeDataLogPageStartWithRetry(
    t_DataHeader *header,
    const t_ImuTagSuperFrame *frame)
{
  enum LOGERR err = writeDataLogPageStart(header, frame);

  if (err == LOGWRITE_ERROR) {
    err = writeDataLogPageStart(header, frame);
  }
  return err;
}

static enum LOGERR writeDataLogSuperFrameWithRetry(
    uint16_t frame_index,
    const t_ImuTagSuperFrame *frame)
{
  enum LOGERR err = writeDataLogSuperFrame(frame_index, frame);

  if (err == LOGWRITE_ERROR) {
    err = writeDataLogSuperFrame(frame_index, frame);
  }
  return err;
}

static enum LOGERR startLogPage(void)
{
  current_page_header.epoch = next_frame_epoch;
  current_page_header.millis = next_frame_millis;
  current_page_header.rawtemp = (int16_t)pState->rawtemp;
  current_frame_index = 0U;
  current_page_logging = discarded_pages >= discard_pages;
  current_page_data_header_written = false;
  current_page_header_written = false;
  page_active = true;

  return LOGWRITE_OK;
}

static enum LOGERR writeCurrentPageInternalHeader(void)
{
  t_DataHeader header;
  enum LOGERR err;

  header.epoch = current_page_header.epoch;
  header.millis =
    (uint16_t)(current_page_header.millis & IMUTAG_HEADER_MILLIS_MASK);
  header.millis |= next_header_flags;
  header.rawtemp = current_page_header.rawtemp;

  err = writeDataHeader(&header);
  if (err == LOGWRITE_OK) {
    pState->cycle_count++;
    next_header_flags = 0U;
    current_page_header_written = true;
  }
  return err;
}

static ImuBlockStatus sampleAndLogDataPage(void)
{
  t_ImuTagSuperFrame frame;
  int16_t env_rawtemp;
  enum LOGERR err;

  if (!page_active) {
    err = startLogPage();
    switch (err) {
    case LOGWRITE_FULL:
      page_active = false;
      return IMU_BLOCK_EXTERNAL_FULL;
    case LOGWRITE_ERROR:
      page_active = false;
      return IMU_BLOCK_EXTERNAL_FULL;
    default:
      break;
    }
  }

  if (!sampleDataCollection(&frame)) {
    return IMU_BLOCK_NO_DATA;
  }

  if (latestDataCollectionRawTemp(&env_rawtemp)) {
    pState->rawtemp = env_rawtemp;
  }

  if (current_page_logging) {
    if (current_frame_index == 0U && !current_page_data_header_written) {
      err = writeDataLogPageStartWithRetry(&current_page_header, &frame);
      switch (err) {
      case LOGWRITE_FULL:
        page_active = false;
        current_page_logging = false;
        current_page_data_header_written = false;
        current_page_header_written = false;
        return IMU_BLOCK_EXTERNAL_FULL;
      case LOGWRITE_ERROR:
        page_active = false;
        current_page_logging = false;
        current_page_data_header_written = false;
        current_page_header_written = false;
        return IMU_BLOCK_EXTERNAL_FULL;
      default:
        current_page_data_header_written = true;
        break;
      }
    } else {
      err = writeDataLogSuperFrameWithRetry(current_frame_index, &frame);
      switch (err) {
      case LOGWRITE_FULL:
        page_active = false;
        current_page_logging = false;
        current_page_data_header_written = false;
        current_page_header_written = false;
        return IMU_BLOCK_EXTERNAL_FULL;
      case LOGWRITE_ERROR:
        page_active = false;
        current_page_logging = false;
        current_page_data_header_written = false;
        current_page_header_written = false;
        return IMU_BLOCK_EXTERNAL_FULL;
      default:
        break;
      }
    }

    if (current_frame_index == 0U && !current_page_header_written) {
      err = writeCurrentPageInternalHeader();
      switch (err) {
      case LOGWRITE_ERROR:
        debug_log_printf(
          "IMUTag running: internal header write error pages=%u ext=%u\r\n",
          (unsigned)pState->pages, (unsigned)pState->external_blocks);
        page_active = false;
        current_page_logging = false;
        current_page_data_header_written = false;
        return IMU_BLOCK_INTERNAL_FULL;
      case LOGWRITE_FULL:
        debug_log_printf(
          "IMUTag running: internal header full pages=%u ext=%u\r\n",
          (unsigned)pState->pages, (unsigned)pState->external_blocks);
        page_active = false;
        current_page_logging = false;
        current_page_data_header_written = false;
        return IMU_BLOCK_INTERNAL_FULL;
      default:
        break;
      }
    }
  }

  current_frame_index++;
  if (current_frame_index < IMUTAG_SUPERFRAMES_PER_PAGE) {
    return IMU_BLOCK_HANDLED;
  }

  if (discarded_pages < discard_pages) {
    discarded_pages++;
    page_active = false;
    current_page_logging = false;
    current_page_data_header_written = false;
    current_page_header_written = false;
  } else {
    err = commitDataLogPage();
    switch (err) {
    case LOGWRITE_FULL:
      page_active = false;
      current_page_logging = false;
      current_page_data_header_written = false;
      current_page_header_written = false;
      return IMU_BLOCK_EXTERNAL_FULL;
    case LOGWRITE_ERROR:
      page_active = false;
      current_page_logging = false;
      current_page_data_header_written = false;
      current_page_header_written = false;
      return IMU_BLOCK_EXTERNAL_FULL;
    default:
      break;
    }
    pState->external_blocks++;
    page_active = false;
    current_page_logging = false;
    current_page_data_header_written = false;
    current_page_header_written = false;
  }

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
    pState->sample_error_count = 0;
    pState->sample_fifo_overruns = 0;
    pState->sample_fifo_watermark_shorts = 0;
    pState->sample_fifo_empty_reads = 0;
    pState->sample_fifo_short_blocks = 0;
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
    const int32_t wake_epoch = timestamp;
    const uint32_t wake_millis = timestamp_millis;
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


    isActive = imuWakeActive();
    if (isActive) {
      for (uint32_t blocks = 0; blocks < IMU_MAX_PAGE_WORK_PER_WAKE; blocks++)
      {
        switch (sampleAndLogDataPage()) {
        case IMU_BLOCK_HANDLED:
          /*
           * The wake that made this superframe available marks the beginning
           * of the next superframe. Use it when that next superframe becomes
           * the first retained frame of a page.
           */
          setNextFrameStartTimestamp(wake_epoch, wake_millis);
          /*
           * Only keep draining while the IMU wake line remains asserted.
           * Without this guard, monitor-attached or shallow-sleep loops can
           * perform speculative FIFO status reads after each flash write.
           */
          isActive = imuWakeActive();
          if (!isActive) {
            blocks = IMU_MAX_PAGE_WORK_PER_WAKE;
          }
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
          blocks = IMU_MAX_PAGE_WORK_PER_WAKE;
          break;
        }
      }
    }
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
    //     Only hibernate on datalog page boundary.

#if CONFIG_HAS_HIBERNATE
    for (size_t i = 0; i < sizeof(sconfig.hibernate) / sizeof(Config_Interval); i++)
    {
      if ((timestamp >= sconfig.hibernate[i].start_epoch) &&
          (timestamp < sconfig.hibernate[i].end_epoch) &&
          !page_active &&
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
