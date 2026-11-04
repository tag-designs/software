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
 *          Timing model: the tag wakes on the RTC minute alarm, and every such
 *          wake writes one 8-byte record: a pressure/temperature reading and
 *          the activity of the minute just ended. The first wake of a run
 *          writes the first checkpoint and record, and that instant anchors
 *          the grid; every later record and every later block boundary is a
 *          fixed offset from the checkpoint in flash.
 *          Nothing about log position is held in RAM, so a reset mid-block
 *          resumes on exactly the same grid, and a checkpoint's epoch is the
 *          time of its own record 0 - which is what lets the host recover a
 *          record's time from its slot with no normalization.
 *
 * @note    Sensor access lives in the tag-local sensors.c behind sensors.h;
 *          this file owns time, state, and log sequencing only.
 *
 * @see     ../design/data-collection.md
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

/** Seconds between records: the RTC minute alarm. */
static const int32_t sample_period = UIUCTAG_RECORD_SECONDS;
/** Seconds covered by one activity bucket. */
static const int32_t bucket_period = UIUCTAG_ACTIVITY_BUCKET_SECONDS;
/** Activity buckets in one record. */
static const int32_t bucket_number = UIUCTAG_ACTIVITY_BUCKETS_PER_RECORD;
/** Bits per activity bucket. */
static const int32_t bucket_bits = UIUCTAG_ACTIVITY_BUCKET_BITS;

/*
 * A block covers UIUCTAG_LOG_RECORDS minutes, but that duration is never
 * needed as a number: a block ends when the grid anchored at its checkpoint
 * runs past its last slot, which is a slot-index comparison.
 */
CASSERT(UIUCTAG_RECORD_SECONDS * UIUCTAG_LOG_RECORDS ==
        UIUCTAG_DATA_LOG_SECONDS);
CASSERT(UIUCTAG_RECORD_SECONDS ==
        UIUCTAG_ACTIVITY_BUCKET_SECONDS * UIUCTAG_ACTIVITY_BUCKETS_PER_RECORD);
/* Bucket counts stop at the bucket period, below the reserved 0x1F. */
CASSERT(UIUCTAG_ACTIVITY_BUCKET_SECONDS < UIUCTAG_ACTIVITY_NOT_OBSERVED);

/**
 * @brief Slot index of an epoch time within its block.
 *
 * @details Measured from the block's own start, which is the checkpoint epoch.
 *          A value at or beyond UIUCTAG_LOG_RECORDS means the block is full and
 *          the next one is due.
 *
 * @param[in] block_start Epoch second of the block's record 0.
 * @param[in] epoch Epoch second at a minute boundary.
 * @return Slot index, which may exceed the block when a block is overdue.
 */
static int32_t slot_of(int32_t block_start, int32_t epoch)
{
  return (epoch - block_start) / sample_period;
}

/**
 * @brief Report whether an epoch time falls on this block's record grid.
 *
 * @details The grid is defined by the checkpoint in flash, not by absolute
 *          time, so this stays true across a reset: a resumed run lands on the
 *          same record times the run would have used had it never stopped.
 *          Every minute alarm is on the grid; an activity-edge wake between
 *          minutes is not.
 *
 * @param[in] block_start Epoch second of the current block's record 0.
 * @param[in] epoch Current epoch second.
 * @return true when a record is due at @p epoch.
 */
static bool on_sample_grid(int32_t block_start, int32_t epoch)
{
  return (epoch >= block_start) &&
         (((epoch - block_start) % sample_period) == 0);
}

/**
 * @brief Global record index of a slot in a given external block.
 *
 * @param[in] block External block index.
 * @param[in] slot Slot index within the block.
 * @return Record index counting from the first record of the log.
 */
static uint32_t record_index_of(uint32_t block, uint32_t slot)
{
  return (block * UIUCTAG_LOG_RECORDS) + slot;
}

/**
 * @brief Accumulate active seconds into the packed buckets of one minute.
 *
 * @details Each active second is attributed to the bucket it falls in, measured
 *          from @p window_start, so the result is correct no matter how the
 *          wakes between @p from and @p to were distributed - activity edge
 *          wakes included. Seconds outside the window are dropped rather than
 *          wrapped: after a missed or very late wake they belong to a minute
 *          whose record has already been written, and folding them in would
 *          corrupt the buckets of the current one.
 *
 * @param[in] activity Packed activity word accumulated so far.
 * @param[in] window_start Epoch second at which the minute being accumulated
 *                         started (the previous record), or INT_MAX when no
 *                         record has been written yet.
 * @param[in] from Epoch second at which the tag became active, or INT_MAX when
 *                 it was inactive.
 * @param[in] to Current epoch second, exclusive.
 * @return Updated packed activity word.
 */
static uint32_t accumulate_activity(uint32_t activity, int32_t window_start,
                                    int32_t from, int32_t to)
{
  /* Before the first record of a run there is no watched minute to attribute
     activity to; that record stores the minute as not observed. */
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

    /* One bucket cannot exceed its period; never let a count reach the
       reserved not-observed value or carry into the neighbouring bucket. */
    if (count >= (uint32_t)bucket_period)
      continue;

    activity += (((uint32_t)1) << shift);
  }
  return activity;
}

/**
 * @brief Open a new external block by appending its internal checkpoint.
 *
 * @param[in] epoch Wake time at which the block is being opened.
 * @param[in] block External block index to record.
 * @return Log write status from the checkpoint write.
 *
 * @post On success pState->pages counts the new checkpoint.
 *       pState->external_blocks is not touched here -- it tracks the
 *       running external record count (set at each successful record
 *       write, in Running()), not the block count, so a caller watching it
 *       for write progress sees it advance every minute rather than only
 *       once per block.
 */
static enum LOGERR open_block(int32_t epoch, uint32_t block)
{
  t_DataHeader header;
  enum LOGERR err;

  header.epoch = epoch;
  header.vdd100 = (uint16_t)pState->vdd100;
  header.extern_log_block = (uint16_t)block;

  err = writeDataHeader(&header);
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
 *          the RTC minute alarm is armed. Each wake accumulates activity; each
 *          minute wake also takes a pressure reading, writes it with the
 *          minute's activity as one record, and opens a new block when the
 *          block window has rolled over.
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
     * No block is opened here. Blocks are opened lazily at the first minute
     * boundary, so every checkpoint describes a block that holds at least one
     * record and the checkpoint epoch is always a real acquisition time.
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

    /*
     * Recovery resumes here, not through T_INIT, and nothing else re-arms the
     * minute alarm. EXCEPTION is a recovered fault. POWERFAIL is a reattach
     * (a monitor attach or an external reset) that the boot classified as a
     * power init: an attach that lands while the tag is awake, rather than in
     * Standby, does exactly that, and deviceInit() then turned every alarm
     * off. Without this the run stayed RUNNING with only the accelerometer
     * armed and stored nothing more -- the harness's first tag-info after a
     * start lands about a second in, while the tag is still awake. A wake
     * from Standby resumes with State_EVENT_OK and keeps its alarm.
     */
    if ((reason == State_EVENT_EXCEPTION) || (reason == State_EVENT_POWERFAIL))
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
     * A record is due when this wake lands on the grid anchored by the current
     * checkpoint. The very first wake of a run has no checkpoint yet and starts
     * the grid here, which is what makes the first minute boundary of the run
     * the reference every later block is measured from.
     */
    if ((events & EVT_RTC_ALRAF) &&
        (!have_block || on_sample_grid(header.epoch, timestamp)))
    {
      t_UIUCTagRecord record;
      uint32_t block;
      uint32_t slot;
      uint32_t index;
      bool new_block;
      enum LOGERR err;

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
                   (int32_t)UIUCTAG_LOG_RECORDS);
      block = have_block ? (uint32_t)header.extern_log_block + (new_block ? 1U : 0U)
                         : 0U;
      slot = new_block ? 0U
                       : (uint32_t)slot_of(header.epoch, timestamp);
      index = record_index_of(block, slot);

      /*
       * Sample the pressure sensor before waking external flash. Both
       * devices share the SPI1 bus mutex (TAG_SPI1_DEVICE_DEFAULTS sets
       * .mutex = &SPI1mutex for each), and HAL_USE_SPI is FALSE for this
       * family, so bus acquisition is a plain chBSemWait() on that one
       * binary semaphore -- not reentrant. dataLogWriteBegin() holds it for
       * the whole write session; sampling the pressure sensor while it was
       * held self-deadlocked this thread permanently on BMP581's own
       * bus-acquire (confirmed: the sample was entered but never returned, no
       * write ever completed, and the tag never reached WFI again). Sample
       * first, while flash is still asleep and the bus is free.
       */
      (void)samplePressureRaw(&record);

      /*
       * The activity word covers the minute that ends now. It was watched
       * from end to end only if the previous record was written exactly one
       * minute ago with no reset in between; otherwise the minute is stored
       * as not observed.
       */
      record.activity = ((lastwrite != INT_MAX) &&
                         (lastwrite == (timestamp - sample_period)))
                            ? (uint16_t)activity
                            : (uint16_t)UIUCTAG_ACTIVITY_UNOBSERVED_WORD;
      activity = 0;

      dataLogWriteBegin();
      err = dataLogWriteRecord(index, &record);
      dataLogWriteEnd();

      if (err != LOGWRITE_OK)
        return Finished(T_INIT, State_EVENT_INTERNALFULL);

      /*
       * Report the running external record count, not the block count:
       * open_block() only advances once per block, so a caller watching
       * external_data_count for write progress would otherwise see it freeze
       * between block boundaries.
       */
      pState->external_blocks = index + 1U;

      /*
       * The checkpoint is written after the record it anchors. A reset in
       * between costs one block of records rather than leaving a checkpoint
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
    //     Only at minute boundaries, so a resumed run always starts a fresh
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

    /*
     * Refresh the level used for this window's close after flash writes, the
     * same way BitTagNG's checkActivitySensorAwake() call before its own
     * final lastactstart update does: the ADXL awake line can change while a
     * pressure sample and header/page write are in progress (easily tens of
     * ms), and using the value read at
     * the top of this wake -- before any of that -- systematically stretched
     * every activity-triggered wake's "active" window all the way to this
     * point regardless of whether the sensor had already gone inactive,
     * pegging the packed activity buckets at their maximum.
     */
    isActive = palReadLine(LINE_WKUP1);

    pState->lastactstart = isActive ? timestamp : INT_MAX;
    pState->activity = activity;
    pState->lastwrite = lastwrite;
    pState->lastwakeup = timestamp;
  }
  return STANDBY;
}
