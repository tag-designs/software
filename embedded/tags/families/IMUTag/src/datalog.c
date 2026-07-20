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
static t_DataLog databuf NOINIT;
static bool datalog_page_cache_active;

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
   * count of valid internal headers. For IMUTag each valid header owns one
   * committed external page image. Incremental page writes can leave one
   * uncommitted page partially programmed, so erase one extra page span when
   * converting headers to dirty sectors.
   */
  const uint64_t dirty_bytes = ((uint64_t)pState->pages + 1U) *
                               (uint64_t)databuf_size;
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
  if ((address + sizeof(vddHeader[index])) > end)
    return false;

#if IMUTAG_STM32U3_FLASH
  return FLASH_Read_Checked(&vddHeader[index].header, header,
                            sizeof(*header)) == 0;
#else
  return FLASH_Read_Checked(&vddHeader[index], header, sizeof(*header)) == 0;
#endif
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
  datalog_page_cache_active = false;
  return 0;
}

static enum LOGERR writeDataLogImmediate(uint32_t page_offset,
                                         const void *data,
                                         size_t size)
{
  int cnt = (int)size;
  int addr = (int)(pState->external_blocks * sizeof(t_DataLog) + page_offset);

  tagStorageWake(TAG_EXTERNAL_FLASH);
  bool ok = tagStorageWrite(TAG_EXTERNAL_FLASH, addr, (uint8_t *) data, &cnt);
  tagStorageSleep(TAG_EXTERNAL_FLASH);

  if (!ok || cnt != (int)size)
  {
    return LOGWRITE_ERROR;
  }

  return LOGWRITE_OK;
}

static enum LOGERR writeDataLogCache(uint32_t page_offset,
                                     const void *data,
                                     size_t size)
{
  uint32_t addr =
      (uint32_t)(pState->external_blocks * sizeof(t_DataLog) + page_offset);
  bool ok;

  tagStorageWake(TAG_EXTERNAL_FLASH);
  if (page_offset == 0U && !datalog_page_cache_active) {
    ok = tagStorageProgramLoad(TAG_EXTERNAL_FLASH, addr, data, (int)size);
    if (ok) {
      datalog_page_cache_active = true;
    }
  } else if (datalog_page_cache_active) {
    ok = tagStorageProgramLoadRandom(TAG_EXTERNAL_FLASH, addr, data,
                                     (int)size);
  } else {
    ok = false;
  }
  tagStorageSleep(TAG_EXTERNAL_FLASH);

  return ok ? LOGWRITE_OK : LOGWRITE_ERROR;
}

static enum LOGERR writeDataLogBytes(uint32_t page_offset,
                                     const void *data,
                                     size_t size)
{
  uint32_t flash_capacity = tagStorageSectorSize(TAG_EXTERNAL_FLASH) *
                            tagStorageSectorCount(TAG_EXTERNAL_FLASH);

  if (data == NULL || size == 0U ||
      (page_offset + size) > sizeof(t_DataLog)) {
    return LOGWRITE_ERROR;
  }

  if ((pState->external_blocks + 1U) > flash_capacity / sizeof(t_DataLog))
  {
    return LOGWRITE_FULL;
  }

  if (tagStorageSupportsProgramCache(TAG_EXTERNAL_FLASH) &&
      page_offset == 0U && !datalog_page_cache_active) {
    enum LOGERR cache_err = writeDataLogCache(page_offset, data, size);
    if (cache_err == LOGWRITE_OK) {
      return LOGWRITE_OK;
    }
  } else if (datalog_page_cache_active) {
    return writeDataLogCache(page_offset, data, size);
  }

  return writeDataLogImmediate(page_offset, data, size);
}

/**
 * @brief Append one data page to external flash at the current log cursor.
 *
 * @param[in] data Data page to write.
 * @return Log write status.
 */
enum LOGERR writeDataLog(t_DataLog *data)
{
  enum LOGERR err = writeDataLogBytes(0U, data, sizeof(*data));

  if (err != LOGWRITE_OK) {
    return err;
  }
  return commitDataLogPage();
}

enum LOGERR writeDataLogPageHeader(t_DataHeader *head)
{
  return writeDataLogBytes(0U, head, sizeof(*head));
}

typedef struct __attribute__((packed)) {
  t_DataHeader header;
  t_ImuTagSuperFrame frame;
} t_ImuTagPageStart;

static_assert(sizeof(t_ImuTagPageStart) ==
                sizeof(t_DataHeader) + sizeof(t_ImuTagSuperFrame),
              "IMUTag page-start write must be tightly packed");

enum LOGERR writeDataLogPageStart(t_DataHeader *head,
                                  const t_ImuTagSuperFrame *frame)
{
  t_ImuTagPageStart page_start;

  if (head == NULL || frame == NULL) {
    return LOGWRITE_ERROR;
  }

  page_start.header = *head;
  page_start.frame = *frame;
  return writeDataLogBytes(0U, &page_start, sizeof(page_start));
}

enum LOGERR writeDataLogSuperFrame(uint16_t frame_index,
                                   const t_ImuTagSuperFrame *frame)
{
  uint32_t page_offset;

  if (frame_index >= IMUTAG_SUPERFRAMES_PER_PAGE) {
    return LOGWRITE_ERROR;
  }

  page_offset = sizeof(t_ImuTagPageHeader) +
                (uint32_t)frame_index * sizeof(t_ImuTagSuperFrame);
  return writeDataLogBytes(page_offset, frame, sizeof(*frame));
}

enum LOGERR commitDataLogPage(void)
{
  uint32_t addr = pState->external_blocks * sizeof(t_DataLog);
  bool ok;

  if (!datalog_page_cache_active) {
    return LOGWRITE_OK;
  }

  tagStorageWake(TAG_EXTERNAL_FLASH);
  ok = tagStorageProgramExecute(TAG_EXTERNAL_FLASH, addr);
  tagStorageSleep(TAG_EXTERNAL_FLASH);

  if (!ok) {
    datalog_page_cache_active = false;
    return LOGWRITE_ERROR;
  }

  datalog_page_cache_active = false;
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

  t_InternalDataHeader slot;
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
  uint32_t header_page = pState->pages;
#endif
  uint32_t *writeptr = (uint32_t *)&vddHeader[pState->pages++];
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
  uint32_t first_flasherr;
  uint32_t retries = 0;
#endif

  memset(&slot, 0xff, sizeof(slot));
#if IMUTAG_STM32U3_FLASH
  slot.header = *head;
#else
  slot = *head;
#endif

  chSysLock();
  FLASH_Unlock();
  uint32_t flasherr =
      FLASH_Program_Array(writeptr, (uint32_t *)&slot, sizeof(slot) / 4);
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
  first_flasherr = flasherr;
#endif
  if (flasherr) {
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
    retries++;
#endif
    flasherr =
        FLASH_Program_Array(writeptr, (uint32_t *)&slot, sizeof(slot) / 4);
  }
  FLASH_Lock();
  FLASH_Flush_Data_Cache();
  chSysUnlock();

 // See if the log file is full

  if ((((uint32_t)writeptr) + sizeof(slot)) >= flashend) {
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
    pState->header_status = LOGWRITE_FULL;
    pState->header_flasherr = first_flasherr ? first_flasherr : flasherr;
    pState->header_page = header_page;
    pState->header_addr = (uint32_t)writeptr;
    pState->header_retries = retries;
#endif
    return LOGWRITE_FULL;
  }
  // See if there is still energy to continue

  if (flasherr) {
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
    pState->header_status = LOGWRITE_ERROR;
    pState->header_flasherr = first_flasherr ? first_flasherr : flasherr;
    pState->header_page = header_page;
    pState->header_addr = (uint32_t)writeptr;
    pState->header_retries = retries;
#endif
    return LOGWRITE_ERROR;
  }
  else {
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
    pState->header_status = LOGWRITE_OK;
    pState->header_flasherr = first_flasherr;
    pState->header_page = header_page;
    pState->header_addr = (uint32_t)writeptr;
    pState->header_retries = retries;
#endif
    return LOGWRITE_OK;
  }
}

//
// Generate monitor ack for data log request
//   Executed by the monitor thread
//

/**
 * @brief Populate and encode a monitor ACK for one IMUTag log page.
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

    tagStorageWake(TAG_EXTERNAL_FLASH);
    tagStorageRead(TAG_EXTERNAL_FLASH, (uint32_t)byte_offset,
                   (uint8_t *)log->samples.bytes, databuf_size);
    tagStorageSleep(TAG_EXTERNAL_FLASH);

    const t_DataLog *page = (const t_DataLog *)log->samples.bytes;
    log->epoch = page->slow_data.epoch;
    log->millisecond =
      (page->slow_data.millis & IMUTAG_HEADER_MILLIS_MASK) |
      (header.millis & (uint16_t)~IMUTAG_HEADER_MILLIS_MASK);
    log->temperature =
      page->slow_data.rawtemp * (float)IMUTAG_PRESSURE_TEMPERATURE_C_PER_LSB;
    log->samples.size = sizeof(t_DataLog);

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
