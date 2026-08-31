/**
 * @file state_run.c
 * @brief UIUCTag RUNNING-state acquisition and logging logic.
 *
 * @details Tag-local replacement for the BitPresTag family RUNNING handler.
 *          UIUCTag stores a different log record than the rest of the family
 *          (see include/uiuctag_log_format.h), so this file diverges from
 *          ../families/BitPresTag/src/state_run.c while the other family
 *          variants keep using the shared version. The common makefile
 *          resolves ./src ahead of the family source directory, so this file
 *          replaces the family one for this target only.
 *
 *          Timing model: the tag wakes on the RTC minute alarm, so every wake
 *          lands on an epoch minute and every sample boundary on an epoch
 *          multiple of the sample period. Activity buckets and sample slots are
 *          therefore indexed from absolute epoch arithmetic rather than from
 *          elapsed time since the run started, which is what lets a reset
 *          resume mid-block without tracking any partial record in RAM.
 *
 * @note    Sensor access lives in the tag-local sensors.c behind sensors.h;
 *          this file owns time, state, and log sequencing only.
 *
 * @see     ../../families/BitPresTag/design/uiuctag-data-collection.md
 */

#include "hal.h"
#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include "app.h"

#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
#include "datalog.h"
#include "devices.h"
#include "sensors.h"

/**
 * @brief Seconds covered by one logged sample.
 *
 * @details Overridable from custom.h so bring-up can run the whole sequence at
 *          a shortened cadence. Values that do not divide the block period
 *          leave the last slot of each block unused.
 */
#ifndef UIUCTAG_SAMPLE_PERIOD_SEC
#define UIUCTAG_SAMPLE_PERIOD_SEC UIUCTAG_EXTERNAL_BLOCK_SECONDS
#endif

/** Seconds between logged samples. */
static const int32_t sample_period = UIUCTAG_SAMPLE_PERIOD_SEC;
/** Seconds covered by one packed activity bucket. */
static const int32_t bucket_period = UIUCTAG_ACTIVITY_BUCKET_SECONDS;
/** Activity buckets packed into one sample. */
static const int32_t bucket_number = UIUCTAG_ACTIVITY_BUCKETS_PER_EXTERNAL_BLOCK;
/** Bits per packed activity bucket. */
static const int32_t bucket_bits = UIUCTAG_ACTIVITY_BUCKET_BITS;
/** Seconds covered by one external block, and so by one internal checkpoint. */
static const int32_t block_period = UIUCTAG_DATA_LOG_SECONDS;

/**
 * @brief Slot index of the sample covering an epoch time.
 *
 * @param[in] epoch Epoch seconds at a sample boundary.
 * @return Slot index within that time's block window.
 */
static uint32_t slot_of(int32_t epoch)
{
  return (uint32_t)((epoch % block_period) / sample_period);
}

/**
 * @brief Global sample index of a slot in a given external block.
 *
 * @param[in] block External block index.
 * @param[in] slot Slot index within the block.
 * @return Sample index counting from the first sample of the log.
 */
static uint32_t sample_index_of(uint32_t block, uint32_t slot)
{
  return (block * UIUCTAG_LOG_SAMPLES) + slot;
}

/**
 * @brief Accumulate active seconds into packed one-minute buckets.
 *
 * @details Each active second is attributed to the bucket its own epoch falls
 *          in, so the result is correct no matter how the wakes between
 *          @p from and @p to were distributed. Accumulation is clamped to the
 *          sample window ending at @p to: after a missed or very late wake the
 *          older seconds belong to a window that has already been written, and
 *          folding them in here would wrap around the bucket count and corrupt
 *          the current sample.
 *
 * @param[in] activity Packed activity word accumulated so far.
 * @param[in] from Epoch second at which the tag became active, or INT_MAX when
 *                 it was inactive.
 * @param[in] to Current epoch second, exclusive.
 * @return Updated packed activity word.
 */
static uint32_t accumulate_activity(uint32_t activity, int32_t from, int32_t to)
{
  int32_t window_start = to - sample_period;

  if (from == INT_MAX)
    return activity;

  if (from < window_start)
    from = window_start;

  for (int32_t i = from; i < to; i++)
  {
    uint32_t shift = (uint32_t)(((i / bucket_period) % bucket_number) *
                                bucket_bits);
    uint32_t count = (activity >> shift) & UIUCTAG_ACTIVITY_BUCKET_MASK;

    /* One bucket cannot exceed its period, but never let a count carry into
       the neighbouring bucket if it somehow would. */
    if (count >= UIUCTAG_ACTIVITY_BUCKET_MASK)
      continue;

    activity += (((uint32_t)1) << shift);
  }
  return activity;
}

/**
 * @brief Write the packed activity word of an already-written sample.
 *
 * @details The activity of a sample only becomes known one sample period after
 *          its pressure was stored, so it is programmed on a later wake. The
 *          target address is recomputed from the timestamp of that earlier
 *          write and the checkpoint that was current at the time, never from a
 *          cursor carried in RAM.
 *
 * @param[in] header Checkpoint describing the block that holds the sample.
 * @param[in] written_at Epoch second at which the sample's pressure was
 *                       written.
 * @param[in] activity Packed activity word to store.
 * @return Log write status.
 *
 * @pre dataLogWriteBegin() has been called.
 */
static enum LOGERR flush_activity(const t_DataHeader *header,
                                  int32_t written_at, uint32_t activity)
{
  uint32_t index = sample_index_of(header->extern_log_block,
                                   slot_of(written_at));

  return dataLogWriteField(index, DATALOG_FIELD_ACTIVITY, &activity);
}

/**
 * @brief Open a new external block by appending its internal checkpoint.
 *
 * @param[in] epoch Wake time at which the block is being opened.
 * @param[in] block External block index to record.
 * @return Log write status from the checkpoint write.
 *
 * @post On success pState->pages counts the new checkpoint and
 *       pState->external_blocks matches, so monitor status reports a valid
 *       download bound.
 */
static enum LOGERR open_block(int32_t epoch, uint32_t block)
{
  t_DataHeader header;
  enum LOGERR err;

  header.epoch = epoch;
  header.vdd100 = (uint16_t)pState->vdd100;
  header.extern_log_block = (uint16_t)block;

  err = writeDataHeader(&header);
  pState->external_blocks = pState->pages;
  return err;
}

/**
 * @brief Read the checkpoint of the most recently opened block.
 *
 * @param[out] header Destination checkpoint.
 * @return true when a written checkpoint exists.
 */
static bool current_block(t_DataHeader *header)
{
  if (pState->pages == 0)
    return false;

  return readDataHeader((int)pState->pages - 1, header) &&
         (header->epoch != -1);
}

/**
 * @brief Handle the UIUCTag data-acquisition state.
 *
 * @details On entry the accelerometer is configured for activity wakeups and
 *          the RTC minute alarm is armed. Each wake accumulates activity; the
 *          wakes that land on a sample boundary also write the previous
 *          sample's activity word, open a new block when the block window has
 *          rolled over, take a pressure sample, and store it.
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
    disableAllAlarms();
    disableTicker();

    // get voltage, internal temperature

    adcVDD(&vdd100, &temp10);

    pState->vdd100 = vdd100;
    pState->temp10 = temp10;

    pState->activity = 0;
    pState->lastwakeup = timestamp;
    pState->lastactstart = INT_MAX;

    /*
     * No block is opened here. Blocks are opened lazily at the first sample
     * boundary, so every checkpoint describes a block that holds at least one
     * sample and the checkpoint epoch is always a real acquisition time.
     */
    pState->lastwrite = INT_MAX;

    pState->state = TagState_RUNNING;
    recordState(reason);

    initDataCollection();

    /*
     * The minute alarm fires on epoch minute boundaries, unlike the periodic
     * wakeup ticker which counts from whenever it was armed. Absolute bucket
     * and slot indexing depends on that alignment.
     */
    enableAlarm(0, ALARM_MINUTE);
  }
  else
  {
    uint32_t activity = pState->activity;
    int32_t lastwrite = pState->lastwrite;
    int32_t lastactstart = pState->lastactstart;
    t_DataHeader header;
    bool have_block;

    // check for completion

    if (sconfig.stop < timestamp)
    {
      return Finished(T_INIT, State_EVENT_ENDTIM);
    }

    if (reason == State_EVENT_EXCEPTION)
    {
      initDataCollection();
      disableAllAlarms();
      disableTicker();
      enableAlarm(0, ALARM_MINUTE);
    }

    // sample once ! -- also used in pwr to decide wakeup edge

    isActive = palReadLine(LINE_WKUP1);

    activity = accumulate_activity(activity, lastactstart, timestamp);

    have_block = current_block(&header);

    if ((events & EVT_RTC_ALRAF) && ((timestamp % sample_period) == 0))
    {
      float pressure_hpa;
      float temperature_c;
      uint32_t block;
      bool new_block;
      enum LOGERR err = LOGWRITE_OK;

      //  update temperature/voltage estimates

      adcVDD(&vdd100, &temp10);
      pState->vdd100 = (pState->vdd100 * 3 + vdd100) / 4;

      new_block = !have_block ||
                  (uiuctagBlockStartEpoch(timestamp) !=
                   uiuctagBlockStartEpoch(header.epoch));
      block = have_block ? (uint32_t)header.extern_log_block + (new_block ? 1U : 0U)
                         : 0U;

      dataLogWriteBegin();

      /*
       * Flush the previous sample's activity first, while the checkpoint that
       * describes its block is still the current one.
       */
      if (have_block && (lastwrite != INT_MAX) &&
          (lastwrite == (timestamp - sample_period)))
      {
        err = flush_activity(&header, lastwrite, activity);
      }
      activity = 0;

      if (err == LOGWRITE_OK)
      {
        samplePressure(&pressure_hpa, &temperature_c);

        err = dataLogWriteField(sample_index_of(block, slot_of(timestamp)),
                                DATALOG_FIELD_PRESSURE, &pressure_hpa);
        if (err == LOGWRITE_OK)
          err = dataLogWriteField(sample_index_of(block, slot_of(timestamp)),
                                  DATALOG_FIELD_TEMPERATURE, &temperature_c);
      }

      dataLogWriteEnd();

      if (err != LOGWRITE_OK)
        return Finished(T_INIT, State_EVENT_INTERNALFULL);

      /*
       * The checkpoint is written after the sample it anchors. A reset in
       * between costs one block of samples rather than leaving a checkpoint
       * that points at data which was never stored.
       */
      if (new_block)
      {
        switch (open_block(timestamp, block))
        {
        case LOGWRITE_FULL:
        case LOGWRITE_ERROR:
          return Finished(T_INIT, State_EVENT_INTERNALFULL);
        case LOGWRITE_BAT:
          // return Finished(T_INIT, State_EVENT_LOWBATTERY);
        default:
          break;
        }
      }

      lastwrite = timestamp;
    }

    //
    // Check for hibernation
    //     Only at sample boundaries, so a resumed run always starts a fresh
    //     block on a clean slot boundary.
    //

    for (size_t i = 0; i < sizeof(sconfig.hibernate) / sizeof(Config_Interval); i++)
    {
      if ((timestamp >= sconfig.hibernate[i].start_epoch) &&
          (timestamp < sconfig.hibernate[i].end_epoch) &&
          ((timestamp % sample_period) == 0))
      {
        pState->activity = 0;
        pState->lastwrite = INT_MAX;
        pState->lastactstart = INT_MAX;
        pState->lastwakeup = timestamp;
        return Hibernating(T_INIT, State_EVENT_STARTHIB);
      }
    }

    pState->lastactstart = isActive ? timestamp : INT_MAX;
    pState->activity = activity;
    pState->lastwrite = lastwrite;
    pState->lastwakeup = timestamp;
  }
  return STANDBY;
}
