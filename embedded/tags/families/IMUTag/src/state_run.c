/**
 * @file state_run.c
 * @brief IMUTag family RUNNING-state acquisition and logging logic.
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
/** @brief Enable hibernation-state support unless a target opts out. */
#define CONFIG_HAS_HIBERNATE 1
#endif

#if !defined(USE_STOP1)
/** @brief Use Stop1 for IMUTag RUNNING-state idle waits by default. */
#define USE_STOP1 1
#endif

/** Number of seconds discarded after IMU trigger-clock restart. */
#define IMU_CLOCK_LOCK_SECONDS 2
/** Maximum log-page work items handled during one RUNNING wake. */
#define IMU_MAX_PAGE_WORK_PER_WAKE 8

/*
 * Maintainer notes
 * ----------------
 *
 * IMUTag logging is more timing-sensitive than the lower-rate tags because the
 * LSM6 FIFO provides an implicit stream of high-rate samples. The log stores
 * sparse internal checkpoints for NAND-backed variants. Each external page
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
 * - abandon any partial pre-reset page by starting the next page at the first
 *   post-warmup block timestamp;
 * - set checkpoint flags so the next internal checkpoint records recovery.
 *
 * Do not use the subsecond field as a normal per-page timing correction. Only
 * the low ten bits are 1/1024-second subsecond ticks, the upper bits are flags,
 * and the rounded millisecond value can add jitter. Host decoding uses the
 * header only for collection start and explicit resync segment anchors.
 */
/** Number of initial pages to discard while the IMU clock/FIFO settles. */
static uint32_t discard_pages;
/** Number of warmup pages already discarded in the current run segment. */
static uint32_t discarded_pages;
/** Header flags to OR into the next internal checkpoint row. */
static uint16_t next_header_flags;
/** True once a log page has been started. */
static bool page_active;
/** True when the current page should be written rather than discarded. */
static bool current_page_logging;
/** True when the external page header/superframe start has been written. */
static bool current_page_data_header_written;
/** True when the internal checkpoint/header for the current page is written. */
static bool current_page_header_written;
/** Header metadata for the active external log page. */
static t_DataHeader current_page_header;
/** Superframe index being filled in the active external log page. */
static uint16_t current_frame_index;
/** Epoch seconds for the next page/superframe anchor. */
static int32_t next_frame_epoch;
/** Millisecond field for the next page/superframe anchor. */
static uint16_t next_frame_millis;

/**
 * @brief Read the IMU wakeup line state.
 *
 * @return true when the IMU wakeup line is asserted.
 */
static bool imuWakeActive(void)
{
  return palReadLine(LINE_WKUP1) == PAL_HIGH;
}

/**
 * @brief Store the timestamp anchor for the next retained frame/page.
 *
 * @param[in] epoch Timestamp seconds.
 * @param[in] millis Timestamp milliseconds; flag bits are masked out.
 */
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
/**
 * @brief Compute how many complete pages to discard after collection restart.
 *
 * @return Number of warmup pages derived from configured ODR.
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

/**
 * @brief Start or restart the IMU data-collection clock.
 *
 * @details @p mark_resync is true only for recovery from a monitor
 *          connect-under-reset while already RUNNING. It causes exactly one
 *          future internal checkpoint to carry IMUTAG_HEADER_RESYNC and
 *          IMUTAG_HEADER_RESTART_RECOVERY. Header flags are cleared only after
 *          that checkpoint write succeeds, so a write failure cannot silently
 *          lose the discontinuity marker.
 *
 * @param[in] mark_resync true when the next checkpoint should mark a recovery
 *                        boundary.
 * @return true when collection hardware was initialized successfully.
 */
static bool restartDataCollectionClock(bool mark_resync)
{
  discard_pages = runDiscardPages();
  discarded_pages = 0;
  next_header_flags = (uint16_t)pState->checkpoint_flags_pending;
  if (mark_resync) {
    next_header_flags |= IMUTAG_HEADER_RESYNC |
                         IMUTAG_HEADER_RESTART_RECOVERY;
  }
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

/**
 * @enum ImuBlockStatus
 * @brief Result of processing one IMU FIFO/logging step.
 */
typedef enum {
  IMU_BLOCK_NO_DATA,       ///< No complete superframe was available.
  IMU_BLOCK_HANDLED,       ///< One superframe or page transition was handled.
  IMU_BLOCK_INTERNAL_FULL, ///< Internal checkpoint storage is full or failed.
  IMU_BLOCK_EXTERNAL_FULL  ///< External NAND storage is full or failed.
} ImuBlockStatus;

/**
 * @brief Start a page write, retrying once on transient storage failure.
 *
 * @param[in] header Header for the page being started.
 * @param[in] frame First superframe for the page.
 * @return Log write status after the initial attempt or retry.
 */
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

/**
 * @brief Write one superframe, retrying once on transient storage failure.
 *
 * @param[in] frame_index Superframe index inside the active page.
 * @param[in] frame Superframe payload to stage.
 * @return Log write status after the initial attempt or retry.
 */
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

/**
 * @brief Initialize bookkeeping for a new external log page.
 *
 * @return LOGWRITE_OK after local page state is reset.
 */
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

/**
 * @brief Write the cadence checkpoint for the page currently being started.
 *
 * @details The checkpoint is written before the external NAND page header and
 *          first superframe. On success, pending recovery flags are cleared so
 *          they are recorded by exactly one checkpoint.
 *
 * @return Log write status from writeDataHeader().
 */
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
    pState->checkpoint_flags_pending = 0;
    next_header_flags = 0U;
    current_page_header_written = true;
  }
  return err;
}

/**
 * @brief Sample at most one superframe and advance the active external page.
 *
 * @details When a retained page starts on an IMUTagNand checkpoint boundary,
 *          this writes the internal checkpoint before loading the external NAND
 *          page cache. Page data is committed only after all superframes for
 *          the page have been staged.
 *
 * @return Status describing whether work completed, no data was available, or
 *         storage became unavailable/full.
 */
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
    if (current_frame_index == 0U && !current_page_header_written &&
        dataLogCheckpointDue()) {
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
 * @brief Handle the IMUTag family data-acquisition state.
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
    /*
     * BROWNOUT arrives here after the state machine recovered a RUNNING marker
     * across a power event, with the log cursors rebuilt by restoreLog(). Zeroing
     * them would restart the log at page 0 and overwrite everything already
     * collected, and a tag browning out repeatedly on a marginal cell would do
     * that on every cycle while never reaching its stop time. Keep the recovered
     * cursors and resynchronize the stream instead.
     */
    const bool resume_after_brownout = (reason == State_EVENT_BROWNOUT);

    disableAllAlarms();
    disableTicker();
    //accelDeinit();

    // Fresh run: reset durable log counters and start a new continuous segment.

    if (!resume_after_brownout)
      pState->pages = 0;
    pState->cycle_count = 0;
    pState->checkpoint_flags_pending = 0;
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

    if (!resume_after_brownout)
      pState->external_blocks = 0;

    pState->state = TagState_RUNNING;
    recordState(reason);
    /*
     * Pass resync=true when resuming so the next retained page carries an
     * explicit discontinuity marker, matching the monitor-reset recovery path.
     */
    if (!restartDataCollectionClock(resume_after_brownout)) {
      debug_log_printf("IMUTag running: abort, collection %s failed\r\n",
                       resume_after_brownout ? "resume" : "init");

      return Aborted(T_INIT, State_EVENT_UNKNOWN);
    }

    debug_log_printf(
        "IMUTag running: collection %s, %d s warmup, pages=%u ext=%u\r\n",
        resume_after_brownout ? "resumed" : "initialized",
        IMU_CLOCK_LOCK_SECONDS, (unsigned)pState->pages,
        (unsigned)pState->external_blocks);
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
          pState->pages = dataLogLastInternalHeaderFlashError();
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
