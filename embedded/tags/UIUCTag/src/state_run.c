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
 *          Timing model: the tag wakes on the RTC minute alarm. The first such
 *          wake of a run writes the first checkpoint and the first sample, and
 *          that instant anchors the sample grid; every later sample and every
 *          later block boundary is a fixed offset from the checkpoint in flash.
 *          Nothing about log position is held in RAM, so a reset mid-block
 *          resumes on exactly the same grid, and a checkpoint's epoch is the
 *          time of its own slot 0 - which is what lets the host recover a
 *          sample's time from its slot with no normalization.
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
 * @details Overridable from custom.h so bring-up can exercise the whole
 *          sequence in minutes instead of hours.
 *
 * @warning An override changes only the firmware's own time base. The host
 *          reconstructs sample times from the geometry in
 *          uiuctag_log_format.h, so a log captured at a shortened period does
 *          not decode to correct absolute times. Use it to watch the write and
 *          wake sequence, not to produce data.
 */
#ifndef UIUCTAG_SAMPLE_PERIOD_SEC
#define UIUCTAG_SAMPLE_PERIOD_SEC UIUCTAG_EXTERNAL_BLOCK_SECONDS
#endif

#if UIUCTAG_SAMPLE_PERIOD_SEC != UIUCTAG_EXTERNAL_BLOCK_SECONDS
#warning "UIUCTAG_SAMPLE_PERIOD_SEC overridden: captured logs will not decode to correct sample times"
#endif

/** Seconds between logged samples. */
static const int32_t sample_period = UIUCTAG_SAMPLE_PERIOD_SEC;
/** Seconds covered by one packed activity bucket. */
static const int32_t bucket_period = UIUCTAG_ACTIVITY_BUCKET_SECONDS;
/** Activity buckets packed into one sample. */
static const int32_t bucket_number = UIUCTAG_ACTIVITY_BUCKETS_PER_EXTERNAL_BLOCK;
/** Bits per packed activity bucket. */
static const int32_t bucket_bits = UIUCTAG_ACTIVITY_BUCKET_BITS;

/*
 * A block covers UIUCTAG_LOG_SAMPLES sample periods, but that duration is never
 * needed as a number: a block ends when the grid anchored at its checkpoint runs
 * past its last slot, which is a slot-index comparison. Deriving a block period
 * from an absolute time base is what previously allowed a shortened sample
 * period to address slots beyond the block.
 *
 * The addressing below assumes the shared geometry is internally consistent.
 */
CASSERT(UIUCTAG_EXTERNAL_BLOCK_SECONDS * UIUCTAG_LOG_SAMPLES ==
        UIUCTAG_DATA_LOG_SECONDS);
CASSERT(UIUCTAG_EXTERNAL_BLOCK_SECONDS ==
        UIUCTAG_ACTIVITY_BUCKET_SECONDS *
            UIUCTAG_ACTIVITY_BUCKETS_PER_EXTERNAL_BLOCK);

/**
 * @brief Slot index of an epoch time within its block.
 *
 * @details Measured from the block's own start, which is the checkpoint epoch.
 *          A value at or beyond UIUCTAG_LOG_SAMPLES means the block is full and
 *          the next one is due.
 *
 * @param[in] block_start Epoch second of the block's slot 0.
 * @param[in] epoch Epoch second at a sample boundary.
 * @return Slot index, which may exceed the block when a block is overdue.
 */
static int32_t slot_of(int32_t block_start, int32_t epoch)
{
  return (epoch - block_start) / sample_period;
}

/**
 * @brief Report whether an epoch time falls on this block's sample grid.
 *
 * @details The grid is defined by the checkpoint in flash, not by absolute
 *          time, so this stays true across a reset: a resumed run lands on the
 *          same sample times the run would have used had it never stopped.
 *          Wakes are minute aligned and the sample period is a whole number of
 *          minutes, so an off-grid wake simply is not a sample boundary.
 *
 * @param[in] block_start Epoch second of the current block's slot 0.
 * @param[in] epoch Current epoch second.
 * @return true when a sample is due at @p epoch.
 */
static bool on_sample_grid(int32_t block_start, int32_t epoch)
{
  return (epoch >= block_start) &&
         (((epoch - block_start) % sample_period) == 0);
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
 * @brief Accumulate active seconds into the packed buckets of one sample.
 *
 * @details Each active second is attributed to the bucket it falls in, measured
 *          from @p window_start, so the result is correct no matter how the
 *          wakes between @p from and @p to were distributed - activity edge
 *          wakes included. Seconds outside the window are dropped rather than
 *          wrapped: after a missed or very late wake they belong to a sample
 *          that has already been written, and folding them in would corrupt the
 *          buckets of the current one.
 *
 * @param[in] activity Packed activity word accumulated so far.
 * @param[in] window_start Epoch second of the sample being accumulated, or
 *                         INT_MAX when no sample has been written yet.
 * @param[in] from Epoch second at which the tag became active, or INT_MAX when
 *                 it was inactive.
 * @param[in] to Current epoch second, exclusive.
 * @return Updated packed activity word.
 */
static uint32_t accumulate_activity(uint32_t activity, int32_t window_start,
                                    int32_t from, int32_t to)
{
  /* Before the first sample of a run there is no slot to attribute activity
     to, so it is discarded rather than credited to the first sample. */
  if ((from == INT_MAX) || (window_start == INT_MAX))
    return activity;

  if (from < window_start)
    from = window_start;

  for (int32_t i = from; i < to; i++)
  {
    int32_t bucket = (i - window_start) / bucket_period;
    uint32_t shift;
    uint32_t count;

    if ((bucket < 0) || (bucket >= bucket_number))
      continue;

    shift = (uint32_t)(bucket * bucket_bits);
    count = (activity >> shift) & UIUCTAG_ACTIVITY_BUCKET_MASK;

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
  int32_t slot = slot_of(header->epoch, written_at);
  uint32_t index;

  /* The sample must belong to the block this checkpoint describes; anything
     else means the caller lost track of which block was current. */
  if ((slot < 0) || (slot >= (int32_t)UIUCTAG_LOG_SAMPLES))
    return LOGWRITE_OK;

  index = sample_index_of(header->extern_log_block, (uint32_t)slot);
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

    activity = accumulate_activity(activity, lastwrite, lastactstart,
                                   timestamp);

    have_block = current_block(&header);

    /*
     * A sample is due when this wake lands on the grid anchored by the current
     * checkpoint. The very first wake of a run has no checkpoint yet and starts
     * the grid here, which is what makes the first minute boundary of the run
     * the reference every later block is measured from.
     */
    if ((events & EVT_RTC_ALRAF) &&
        (!have_block || on_sample_grid(header.epoch, timestamp)))
    {
      float pressure_hpa;
      float temperature_c;
      uint32_t block;
      uint32_t slot;
      bool new_block;
      enum LOGERR err = LOGWRITE_OK;

      //  update temperature/voltage estimates

      adcVDD(&vdd100, &temp10);
      pState->vdd100 = (pState->vdd100 * 3 + vdd100) / 4;

      /*
       * The block is full once the grid runs past its last slot. A long gap -
       * hibernation, or a stopped clock - lands well past it, and re-anchors
       * the grid at this wake rather than leaving the skipped slots addressable.
       */
      new_block = !have_block ||
                  (slot_of(header.epoch, timestamp) >=
                   (int32_t)UIUCTAG_LOG_SAMPLES);
      block = have_block ? (uint32_t)header.extern_log_block + (new_block ? 1U : 0U)
                         : 0U;
      slot = new_block ? 0U
                       : (uint32_t)slot_of(header.epoch, timestamp);

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

        err = dataLogWriteField(sample_index_of(block, slot),
                                DATALOG_FIELD_PRESSURE, &pressure_hpa);
        if (err == LOGWRITE_OK)
          err = dataLogWriteField(sample_index_of(block, slot),
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
          (!have_block || on_sample_grid(header.epoch, timestamp)))
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
