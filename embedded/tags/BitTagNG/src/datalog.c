#include "app.h"
#include "datalog.h"
#include "devices.h"
#include "flash_internal.h"
#include "storage_flash.h"
#include <stdbool.h>
#include <tag.pb.h>
#include "persistent.h"

const int databuf_size = sizeof(t_DataLog);
static t_DataLog databuf NOINIT;

static volatile int sectors_erased NOINIT;
static uint32_t erase_sector_size;
static uint32_t erase_sector_total;
static bool erase_external_active;

extern int encode_ack(void);

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

static int countInternalBlocks(void){
  int count = 0;
  t_DataHeader header;

  while (readDataHeader(count, &header)) {
      if (header.epoch == -1)
          break;
      count++;
  }
  return count;
}

static uint32_t dirtyExternalSectors(void)
{
  const uint32_t sector_size = tagStorageSectorSize(TAG_EXTERNAL_FLASH);
  const uint32_t sector_count = tagStorageSectorCount(TAG_EXTERNAL_FLASH);
  /*
   * Reset calls restoreLog() before eraseExternal(), so pState->pages is the
   * count of valid internal headers. For BitTagNG each valid header owns one
   * external page: a t_DataLog containing DATALOG_SAMPLES activity words.
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

void eraseExternalBlock(void){
  int32_t addr;
  const uint32_t sector_size = tagStorageSectorSize(TAG_EXTERNAL_FLASH);
  
  // round up to full sector

  addr = pState->external_blocks*2;

  tagStorageWake(TAG_EXTERNAL_FLASH);
  tagStorageSectorErase(TAG_EXTERNAL_FLASH, addr);
  tagStorageSleep(TAG_EXTERNAL_FLASH);
  if (sector_size != 0U) {
    pState->external_blocks = (addr / sector_size) * (sector_size / 2U);
  }
}

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
  const uint32_t dirty_sectors = dirtyExternalSectors();

  erase_sector_size = sector_size;
  erase_sector_total = dirty_sectors;
  sectors_erased = 0;
  erase_external_active = false;

  if (sector_size == 0U || dirty_sectors == 0U)
    return;

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

uint32_t externalFlashSize(void)
{
  return tagStorageSectorSize(TAG_EXTERNAL_FLASH) *
         tagStorageSectorCount(TAG_EXTERNAL_FLASH);
}

int externalFlashSectorsErased(void)
{
  return sectors_erased;
}

int externalFlashSectorsToErasePlusOne(void)
{
  return (int)dirtyExternalSectors() + 1;
}

// Recover pState from log

int restoreLog(void)
{
  const uint32_t recovered_external_blocks = pState->external_blocks;
  pState->pages = countInternalBlocks();
  const uint32_t max_external_blocks = pState->pages * DATALOG_SAMPLES;
  pState->external_blocks =
      (recovered_external_blocks <= max_external_blocks)
          ? recovered_external_blocks
          : max_external_blocks;
  return 0;
}


// 
// Write data to external log
//

enum LOGERR writeDataLog(uint16_t *data, int num)
{
  uint32_t flash_capacity = externalFlashSize();
  if ((pState->external_blocks * 2) + (uint32_t)(num * 2) > flash_capacity)
  {
    return LOGWRITE_FULL;
  }

  int cnt = num * 2;
  int addr = pState->external_blocks * 2;

  tagStorageWake(TAG_EXTERNAL_FLASH);
  bool ok = tagStorageWrite(TAG_EXTERNAL_FLASH, addr, (uint8_t *)data, &cnt);
  tagStorageSleep(TAG_EXTERNAL_FLASH);
  if (!ok || cnt != (num * 2))
  {
    return LOGWRITE_ERROR;
  }
  pState->external_blocks += num;
  return LOGWRITE_OK;
}

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

  if ((((uint32_t)writeptr) + 8) >= flashend)
    return LOGWRITE_FULL;
  // See if there is still energy to continue

  if (flasherr) 
    return LOGWRITE_ERROR;
  if (head->vdd100 < 200)
    return LOGWRITE_BAT;
  else
    return LOGWRITE_OK;
}

//
// Generate monitor ack for data log request
//   Executed by the monitor thread
//

int data_logAck(int index, Ack *ack)
{

  BitTagNgLog *data = &ack->payload.bittag_ng_data_log;
  ack->err = Ack_Err_OK;

  t_DataHeader header;
  uint32_t page_start = (uint32_t)index * DATALOG_SAMPLES;
  uint32_t page_count = 0;
  uint64_t byte_offset = (uint64_t)databuf_size * (uint64_t)index;
  bool valid_index =
      (index >= 0) &&
      readDataHeader(index, &header) &&
      (header.epoch != -1) &&
      (byte_offset + sizeof(databuf) <= (uint64_t)externalFlashSize());

  if (valid_index)
  {
    if (pState->external_blocks > page_start)
    {
      page_count = pState->external_blocks - page_start;
      if (page_count > DATALOG_SAMPLES)
        page_count = DATALOG_SAMPLES;

      tagStorageWake(TAG_EXTERNAL_FLASH);
      tagStorageRead(TAG_EXTERNAL_FLASH, (uint32_t)byte_offset,
                     (uint8_t *)&databuf, page_count * sizeof(databuf.activity[0]));
      tagStorageSleep(TAG_EXTERNAL_FLASH);
    }

    data->epoch = header.epoch;
    data->voltage = header.vdd100 * 0.01f;
    data->temperature = header.temp10 * 0.1f;
    data->activity_count = 0;

    for (uint32_t j = 0; j < page_count; j++) // loop over samples
    {
      data->activity[j] = databuf.activity[j];
      data->activity_count++;
    }
    ack->which_payload = Ack_bittag_ng_data_log_tag;
  }
  else
  {
    ack->err = Ack_Err_NODATA;
    ack->which_payload = 0;
  }

  // encode the ack and return

  return encode_ack();
}
