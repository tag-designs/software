/**
 * @file datalog.c
 * @brief PresTag log storage, erase support, and monitor ACK export.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "app.h"
#include "datalog.h"
#include "devices.h"
#include "flash_internal.h"
#include "lps27hhw.h"
#include "storage_flash.h"
#include <stdbool.h>
#include <string.h>
#include <tag.pb.h>
#include "persistent.h"

#ifndef PRESTAG_RAW_LOG
#define PRESTAG_RAW_LOG 0
#endif

#define PRESTAG_LPS27_PRESSURE_CONSTANT (1.0f / 16.0f)
#define PRESTAG_LPS27_TEMPERATURE_CONSTANT (1.0f / 100.0f)

const int databuf_size = sizeof(t_DataLog);
static t_DataLog databuf NOINIT;
static volatile int sectors_erased NOINIT;
static uint32_t erase_sector_size;
static uint32_t erase_sector_total;
static bool erase_external_active;

#if PRESTAG_RAW_LOG
_Static_assert(sizeof(t_PresTagRawBlock) ==
                   sizeof(int32_t) + (2 * sizeof(float)) + sizeof(t_DataLog),
               "PresTag raw block layout has unexpected padding");
_Static_assert(sizeof(((PresTagRawLog *)0)->samples.bytes) >= sizeof(t_PresTagRawBlock),
               "nanopb PresTagRawLog.samples buffer cannot hold one raw block");
#endif

/**
 * @brief Raise MSI clock speed while formatting large monitor log responses.
 */
static void fast_msi(void){
  // change to 24Mhz doesn't require VOS change
  // Adjust Wait States

  FLASH->ACR = (FLASH->ACR & ~(7)) | 3;

  // Change MSI frequency P 197 RM0394

  RCC->CR = (RCC->CR & ~(15<<4)) | (9<<4);

  // Change TIM2 Prescaler

  STM32_ST_TIM->PSC = STM32_ST_TIM->PSC * 12;

}


/**
 * @brief Restore the normal low-power MSI clock after monitor log export.
 */
static void slow_msi(void){

 
   // Restore MSI frequency P 197 RM0394

   RCC->CR = (RCC->CR & ~(15<<4)) | (5<<4);

  // Adjust Wait States

  FLASH->ACR = FLASH->ACR & ~(7);

  // Restore TIM2 Prescaler

  STM32_ST_TIM->PSC = STM32_ST_TIM->PSC/12;

}



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

/**
 * @brief Count valid internal flash headers after reset.
 *
 * @return Number of written internal header blocks.
 */
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
   * count of valid internal headers. For PresTag each valid header owns exactly
   * one external page: a t_DataLog containing DATALOG_SAMPLES pressure samples.
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
   * Erase only sectors touched by pages with valid internal headers. Scanning
   * the flash contents is slower and can make monitor erase progress disagree
   * with the actual log pages that will be downloaded.
   */
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
  pState->pages = countInternalBlocks();
  pState->external_blocks = pState->pages * DATALOG_SAMPLES;
  return 0;
}

/**
 * @brief Append sample words to external flash at the current log cursor.
 *
 * @param[in] data Words to write.
 * @param[in] num Number of 16-bit words to write.
 * @return Log write status.
 */
enum LOGERR writeDataLog(uint16_t *data, int num)
{
  uint32_t flash_capacity = tagStorageSectorSize(TAG_EXTERNAL_FLASH) *
                            tagStorageSectorCount(TAG_EXTERNAL_FLASH);
  if ((pState->external_blocks * 4) + (uint32_t)(num * 2) > flash_capacity)
  {
    return LOGWRITE_FULL;
  }

  int cnt = num*2;
  int addr = pState->external_blocks * 4;

  tagStorageWake(TAG_EXTERNAL_FLASH);
  tagStorageWrite(TAG_EXTERNAL_FLASH, addr, (uint8_t *)data, &cnt);
  tagStorageSleep(TAG_EXTERNAL_FLASH);
  
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
  if (head->vdd100[0] < 200)
    return LOGWRITE_BAT;
  else
    return LOGWRITE_OK;
}

//
// Generate monitor ack for data log request
//   Executed by the monitor thread
//

/**
 * @brief Populate and encode a monitor ACK for one PresTag log page.
 *
 * @param[in] index Log page index to export.
 * @param[out] ack ACK message to fill.
 * @return Encoded ACK length.
 */
int data_logAck(int index, Ack *ack)
{
  int ret;
  chThdSetPriority(HIGHPRIO);
  fast_msi();
#if PRESTAG_RAW_LOG
  PresTagRawLog *data = &ack->payload.prestag_raw_data_log;
#else
  PresTagLog *data = &ack->payload.prestag_data_log;
#endif
  ack->err = Ack_Err_OK;

  uint32_t persistent_end = (uint32_t)&__persistent_end__;
#if PRESTAG_RAW_LOG
  data->temp_constant = PRESTAG_LPS27_TEMPERATURE_CONSTANT;
  data->pres_constant = PRESTAG_LPS27_PRESSURE_CONSTANT;
  data->samples.size = 0;

  if (index >= 0)
  {
    tagStorageWake(TAG_EXTERNAL_FLASH);
    while ((data->samples.size + sizeof(t_PresTagRawBlock)) <=
           sizeof(data->samples.bytes))
    {
      const int current_index =
          index + (int)(data->samples.size / sizeof(t_PresTagRawBlock));
      const uint64_t byte_offset = sizeof(databuf) * (uint64_t)current_index;
      t_DataHeader header;
      t_PresTagRawBlock block;

      if (((uint32_t)&vddHeader[current_index] >= persistent_end) ||
          !readDataHeader(current_index, &header) ||
          (header.epoch == -1) ||
          (byte_offset + sizeof(databuf) > (uint64_t)externalFlashSize()))
      {
        break;
      }

      tagStorageRead(TAG_EXTERNAL_FLASH, (uint32_t)byte_offset,
                     (uint8_t *)&databuf, sizeof(databuf));

      block.epoch = header.epoch;
      block.voltage = header.vdd100[0] * 0.01f;
      block.temperature = header.vdd100[1] * 0.01f;
      memcpy(&block.samples, &databuf, sizeof(block.samples));
      memcpy(&data->samples.bytes[data->samples.size], &block, sizeof(block));
      data->samples.size += sizeof(block);
    }
    tagStorageSleep(TAG_EXTERNAL_FLASH);
  }

  ack->which_payload =
      (data->samples.size > 0) ? Ack_prestag_raw_data_log_tag : 0;
#else
  uint64_t byte_offset = sizeof(databuf) * (uint64_t)index;
  t_DataHeader header;
  bool valid_index =
      (index >= 0) &&
      ((uint32_t)&vddHeader[index] < persistent_end) &&
      readDataHeader(index, &header) &&
      (header.epoch != -1) &&
      (byte_offset + sizeof(databuf) <= (uint64_t)externalFlashSize());

  if (valid_index)
  {
    tagStorageWake(TAG_EXTERNAL_FLASH);
    tagStorageRead(TAG_EXTERNAL_FLASH, (uint32_t)byte_offset,
                   (uint8_t *)&databuf, sizeof(databuf));
    tagStorageSleep(TAG_EXTERNAL_FLASH);

    ack->which_payload = Ack_prestag_data_log_tag;
    data->epoch = header.epoch;
    data->voltage = header.vdd100[0] * 0.01f;
    data->temperature = header.vdd100[1] * 0.01f;
    data->data_count = 0;

    for (int j = 0; j < DATALOG_SAMPLES; j++) // loop over samples
    {
      if (databuf.data[j].pressure == -1)
        break;
      data->data[j].pressure = lps27Pressure(databuf.data[j].pressure);
      data->data[j].temperature =
          lps27Temperature(databuf.data[j].temperature);
      data->data_count++;
    }
  }
  else
  {
    ack->which_payload = 0;
  }
#endif

  // encode the ack and return
  ret = encode_ack();
  slow_msi();
  chThdSetPriority(NORMALPRIO);
  return ret;
}
