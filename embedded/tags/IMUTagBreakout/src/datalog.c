/**
 * @file datalog.c
 * @brief IMUTagBreakout log storage, erase support, and monitor ACK export.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "app.h"
#include "datalog.h"
#include "flash_internal.h"
#include <string.h>
#include <tag.pb.h>
#include "devices.h"
#include "persistent.h"

const int databuf_size = DATALOG_SAMPLES * sizeof(t_DataLog);
static t_DataLog databuf[DATALOG_SAMPLES] NOINIT;

static_assert(sizeof(((IMUTagRawLog*)0)->samples.bytes) == DATALOG_SAMPLES * sizeof(t_DataLog),
              "nanopb IMUTagRawLog.samples buffer size in options is out of sync with datalog page size!");
static volatile int sectors_erased NOINIT;
static uint32_t erase_sector_size;
static uint32_t erase_sector_total;
static bool erase_external_active;

extern int encode_ack(void);

#if defined(LOG_ACK_MEASURE_LINE)
static bool log_ack_measure_line_initialized;

static void logAckMeasureBegin(void)
{
  if (!log_ack_measure_line_initialized) {
    palClearLine(LOG_ACK_MEASURE_LINE);
    palSetLineMode(LOG_ACK_MEASURE_LINE, PAL_MODE_OUTPUT_PUSHPULL);
    log_ack_measure_line_initialized = true;
  }
  palSetLine(LOG_ACK_MEASURE_LINE);
}

static void logAckMeasureEnd(void)
{
  palClearLine(LOG_ACK_MEASURE_LINE);
}
#else
#define logAckMeasureBegin() do { } while (0)
#define logAckMeasureEnd() do { } while (0)
#endif

static uint32_t dirtyExternalSectors(void)
{
  const uint32_t sector_size = tagStorageSectorSize(TAG_EXTERNAL_FLASH);
  const uint32_t sector_count = tagStorageSectorCount(TAG_EXTERNAL_FLASH);
  /*
   * Reset calls restoreLog() before eraseExternal(), so pState->pages is the
   * count of valid internal headers. For IMUTag each valid header owns exactly
   * one external page: DATALOG_SAMPLES 128-byte t_DataLog blocks.
   */
  const uint64_t dirty_bytes = (uint64_t)pState->pages * (uint64_t)databuf_size;
  uint32_t dirty_sectors;

  if (sector_size == 0U || sector_count == 0U) {
    return 0;
  }

  dirty_sectors = (uint32_t)((dirty_bytes + sector_size - 1U) / sector_size);
  if (dirty_sectors > sector_count) {
    dirty_sectors = sector_count;
  }
  return dirty_sectors;
}

static bool readDataHeader(int index, t_DataHeader *header)
{
  uint32_t end = (uint32_t)&__persistent_end__;

  if (index < 0)
    return false;

  uint32_t address = (uint32_t)&vddHeader[index];
  if ((address + sizeof(*header)) > end)
    return false;

  return FLASH_Read_Checked(&vddHeader[index], header, sizeof(*header)) == 0;
}

static size_t writtenDataBlocks(const t_DataLog *blocks, size_t max_blocks)
{
  size_t i;

  for (i = 0; i < max_blocks; i++) {
    if (!IMUTAG_BLOCK_IS_WRITTEN(blocks[i].flags))
      break;
  }
  return i;
}

/**
 * @brief Erase the external data log and reset log progress.
 */
void eraseExternal()
{
  eraseExternalStart();
  while (eraseExternalNextSector())
    chThdYield();
  eraseExternalFinish();
}

void eraseExternalStart(void)
{
  const uint32_t sector_size = tagStorageSectorSize(TAG_EXTERNAL_FLASH);
  /*
   * Erase only the sectors touched by pages with valid internal headers.
   * Walking the whole 16 MiB device would waste time and would make monitor
   * erase progress misleading. dirtyExternalSectors() is also the status
   * progress denominator, so firmware erase work and host progress agree.
   */
  const uint32_t dirty_sectors = dirtyExternalSectors();

  erase_sector_size = sector_size;
  erase_sector_total = dirty_sectors;
  sectors_erased = 0;
  erase_external_active = false;

  if (sector_size == 0U || dirty_sectors == 0U) {
    return;
  }

  erase_external_active = true;
  tagStorageWake(TAG_EXTERNAL_FLASH);
}

bool eraseExternalNextSector(void)
{
  if (!erase_external_active)
    return false;

  if ((uint32_t)sectors_erased < erase_sector_total) {
    tagStorageSectorErase(TAG_EXTERNAL_FLASH,
                          (uint32_t)sectors_erased * erase_sector_size);
    sectors_erased++;
  }

  return (uint32_t)sectors_erased < erase_sector_total;
}

void eraseExternalFinish(void)
{
  if (erase_external_active)
    tagStorageSleep(TAG_EXTERNAL_FLASH);
  erase_external_active = false;
  pState->external_blocks = 0;
  sectors_erased = 0;
}

/**
 * @brief Return external flash capacity in bytes.
 *
 * @return External storage capacity.
 */
uint32_t externalFlashSize(void)
{
  return tagStorageSectorSize(TAG_EXTERNAL_FLASH) *
         tagStorageSectorCount(TAG_EXTERNAL_FLASH);
}

/**
 * @brief Report current erase progress for monitor polling.
 *
 * @return Number of sectors processed in the active erase.
 */
int externalFlashSectorsErased(void)
{
  return sectors_erased;
}

int externalFlashSectorsToErasePlusOne(void)
{
  return (int)dirtyExternalSectors() + 1;
}

/**
 * @brief Recover persistent log cursors from internal flash headers.
 *
 * @return 0 when recovery completes.
 */
int restoreLog(void)
{
  int i;
  t_DataHeader header;
  for (i = 0; readDataHeader(i, &header); i++)
  {
    if (header.epoch == -1)
      break;
  }
  pState->pages = i;
  pState->cycle_count = pState->pages;
  pState->external_blocks = pState->pages * DATALOG_SAMPLES;
  return 0;
}

/**
 * @brief Append one data block to external flash at the current log cursor.
 *
 * @param[in] data Data block to write.
 * @return Log write status.
 */
enum LOGERR writeDataLog(t_DataLog *data)
{
  uint32_t flash_capacity = tagStorageSectorSize(TAG_EXTERNAL_FLASH) *
                            tagStorageSectorCount(TAG_EXTERNAL_FLASH);

  if ((pState->external_blocks + 1U) > flash_capacity / sizeof(t_DataLog))
  {
    return LOGWRITE_FULL;
  }

  int cnt = sizeof(t_DataLog);
  int addr = pState->external_blocks * sizeof(t_DataLog);

  tagStorageWake(TAG_EXTERNAL_FLASH);
  bool ok = tagStorageWrite(TAG_EXTERNAL_FLASH, addr, (uint8_t *) data, &cnt);
  tagStorageSleep(TAG_EXTERNAL_FLASH);

  if (!ok || cnt != (int)sizeof(t_DataLog))
  {
    return LOGWRITE_ERROR;
  }

  return LOGWRITE_OK;
}

/**
 * @brief Persist an internal flash header for the next external log page.
 *
 * @param[in] head Header to write.
 * @return Log write status.
 */
extern enum LOGERR writeDataHeader(t_DataHeader *head)
{
  uint32_t flashend = (uint32_t)&__persistent_end__;

  uint32_t *writeptr = (uint32_t *)&vddHeader[pState->pages++];

  chSysLock();
  FLASH_Unlock();
  uint32_t flasherr = FLASH_Program_Array(writeptr, (uint32_t *) head, sizeof(t_DataHeader)/4);
  FLASH_Lock();
  FLASH_Flush_Data_Cache();
  chSysUnlock();

 // See if the log file is full

  if ((((uint32_t)writeptr) + 16) >= flashend)
    return LOGWRITE_FULL;
  // See if there is still energy to continue

  if (flasherr) 
    return LOGWRITE_ERROR;
  else
    return LOGWRITE_OK;
}

//
// Generate monitor ack for data log request
//   Executed by the monitor thread
//

/**
 * @brief Populate and encode a monitor ACK for one IMUTagBreakout log page.
 *
 * @param[in] index Log page index to export.
 * @param[out] ack ACK message to fill.
 * @return Encoded ACK length.
 */
int data_logAck(int index, Ack *ack)
{
  int ret;

  chThdSetPriority(HIGHPRIO);

  ack->err = Ack_Err_OK;

  uint32_t end = (uint32_t)&__persistent_end__;
  uint64_t byte_offset = sizeof(databuf) * (uint64_t)index;
  t_DataHeader header;

  // check for valid header

  if (index >= 0 && ((uint32_t)&vddHeader[index] < end) &&
      readDataHeader(index, &header) &&
      (header.epoch != -1) &&
      (byte_offset + sizeof(databuf) <= (uint64_t)externalFlashSize()))
  {

    // we have a valid header

    ack->which_payload = Ack_imu_raw_data_log_tag;
    IMUTagRawLog *log = &ack->payload.imu_raw_data_log;

    log->epoch = header.epoch;
    log->millisecond = header.millis;
    log->temperature = header.rawtemp * 0.01f;

    // now read the data --- optimization is to read this directly into databuf!

    tagStorageWake(TAG_EXTERNAL_FLASH);
    tagStorageRead(TAG_EXTERNAL_FLASH, (uint32_t)byte_offset,
                   (uint8_t *)log->samples.bytes, databuf_size);
    tagStorageSleep(TAG_EXTERNAL_FLASH);

    log->samples.size =
      writtenDataBlocks((const t_DataLog *)log->samples.bytes,
                        DATALOG_SAMPLES) * sizeof(t_DataLog);
    //memcpy(log->samples.bytes, databuf, sizeof(databuf));

  }
  else
  {
    ack->which_payload = 0;
  }

  // encode the ack and return
  logAckMeasureBegin();
  ret = encode_ack();
  logAckMeasureEnd();
  chThdSetPriority(NORMALPRIO);
  return ret;
}
