/**
 * @file datalog.c
 * @brief UIUCTag log storage, erase support, and monitor ACK export.
 * @author tag firmware authors
 * @date 2026-08-31
 *
 * @details Tag-local replacement for the BitPresTag family log module. UIUCTag
 *          stores compact float pressure/activity records instead of the
 *          family layout (see include/uiuctag_log_format.h), so this file
 *          diverges from ../families/BitPresTag/src/datalog.c while the other
 *          family variants keep using the shared version. The common makefile
 *          resolves ./src ahead of the family source directory, so this file
 *          replaces the family one for this target only.
 *
 * @note    Sample fields are programmed individually as they become available:
 *          pressure and temperature when a sample is taken, and the packed
 *          activity word one sample period later. Slot addresses come from the
 *          acquisition clock, so this module never has to track a partial
 *          record in RAM and a reset mid-block cannot re-program a written
 *          field.
 *
 * @see     ../../families/BitPresTag/design/uiuctag-data-collection.md
 */

#include "app.h"
#include "datalog.h"
#include "devices.h"
#include "flash_internal.h"


#include "storage_flash.h"
#include <stdbool.h>
#include <string.h>
#include <tag.pb.h>
#include "persistent.h"
#include "custom.h"

/** Exported for shared monitor code that reports download block size. */
const int databuf_size = DATALOG_BLOCK_BYTES;
/** Sectors processed so far by the incremental external erase. */
static volatile int sectors_erased NOINIT;
/** Total sectors the active erase sequence has to cover. */
static uint32_t erase_sector_total;
/** True while an incremental external erase is in progress. */
static bool erase_external_active;

/**
 * @brief Raise MSI clock speed while formatting large monitor log responses.
 */
static void fast_msi(void){
  // change to 24Mhz doesn't require VOS change
  // Adjust Wait States

  FLASH->ACR = (FLASH->ACR & ~(7)) | FLASH_WS_FAST;

  // Change MSI frequency P 197 RM0394

  RCC->CR = (RCC->CR & ~(15<<4)) |  STM32_MSIRANGE_FAST;

  // Change TIM2 Prescaler

  STM32_ST_TIM->PSC =  ((STM32_TIMCLK2 * RANGE_MULTIPLIER)/ OSAL_ST_FREQUENCY) - 1;;

}


/**
 * @brief Restore the normal low-power MSI clock after monitor log export.
 */
static void slow_msi(void){

 
   // Restore MSI frequency P 197 RM0394

   RCC->CR = (RCC->CR & ~(15<<4)) | STM32_MSIRANGE;

  // Adjust Wait States

  FLASH->ACR = (FLASH->ACR & ~(7)) | FLASH_WS_SLOW;

  // Restore TIM2 Prescaler

  STM32_ST_TIM->PSC =  (STM32_TIMCLK2 / OSAL_ST_FREQUENCY) - 1;

}



/**
 * @brief Encode the staged monitor ACK into the protocol transmit buffer.
 *
 * @details Provided by the monitor module; declared here because the log
 *          response is formatted in this file but transmitted by the monitor.
 *
 * @return Encoded ACK length in bytes.
 */
extern int encode_ack(void);

/* Public API contract documented in datalog.h. */
bool readDataHeader(int index, t_DataHeader *header)
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
 * @brief Count written internal checkpoints after reset.
 *
 * @details Linear scan from index 0 to the first unwritten slot. The array runs
 *          to the end of the persistent region rather than stopping at the
 *          declared vddHeader[] extent, so this is bounded by the region, not by
 *          256 entries. It runs only on the power-on/brownout/exception
 *          recovery path, never on an ordinary standby wake.
 *
 * @return Number of written checkpoints.
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

/**
 * @brief Erase one external sector if it contains programmed data.
 *
 * @param[in] sector Sector index to inspect.
 * @return true when the sector was erased.
 */
static bool eraseExternalSector(int sector){
  int32_t addr;
  uint8_t buf[256];
  int sector_size = tagStorageSectorSize(TAG_EXTERNAL_FLASH);
  int sector_count = tagStorageSectorCount(TAG_EXTERNAL_FLASH);

  // round up to full sector

  if (sector < 0 || sector >= sector_count)
    return false;

  addr = sector * sector_size;

  // read a buffer
  tagStorageRead(TAG_EXTERNAL_FLASH, addr, buf, 256);
  for (int i = 0; i < 256; i++) {
      if (buf[i] != 255) {
        tagStorageSectorErase(TAG_EXTERNAL_FLASH, addr);
        return true;
      }
  }
  return false;
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

/**
 * @brief Begin an incremental erase of the external data log.
 *
 * @details Splitting the erase across calls keeps the monitor responsive and
 *          lets the caller poll progress, since erasing a 4 MB flash takes far
 *          longer than one monitor transaction may block.
 *
 * @post External flash is awake while the sequence is active; the erase is a
 *       no-op when the device reports no sectors.
 */
void eraseExternalStart(void)
{
  erase_sector_total = tagStorageSectorCount(TAG_EXTERNAL_FLASH);
  sectors_erased = 0;
  erase_external_active = false;

  if (erase_sector_total == 0U)
    return;

  erase_external_active = true;
  tagStorageWake(TAG_EXTERNAL_FLASH);
}

/**
 * @brief Erase the next sector of the active erase sequence.
 *
 * @return true when more sectors remain, false when the sequence is complete or
 *         was never started.
 */
bool eraseExternalNextSector(void)
{
  if (!erase_external_active)
    return false;

  if ((uint32_t)sectors_erased < erase_sector_total) {
    eraseExternalSector(sectors_erased);
    sectors_erased++;
  }

  return (uint32_t)sectors_erased < erase_sector_total;
}

/**
 * @brief Complete an incremental external erase and reset the log cursor.
 *
 * @post External flash is asleep and pState->external_blocks is zero, so the
 *       next run starts at block 0.
 */
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

/**
 * @brief Report the erase-progress denominator used by monitor status.
 *
 * @details One more than the sector count, so a progress display can show a
 *          distinct "finishing" step after the last sector is erased.
 *
 * @return Sector count plus one.
 */
int externalFlashSectorsToErasePlusOne(void)
{
  return tagStorageSectorCount(TAG_EXTERNAL_FLASH) + 1;
}

/**
 * @brief Recover persistent log cursors from internal flash headers.
 *
 * @return 0 when recovery completes.
 */
int restoreLog(void)
{
  pState->pages = countInternalBlocks();
  /*
   * One checkpoint always describes exactly one external block, so the block
   * count and the checkpoint count are the same number. Reporting blocks here
   * also keeps Status.external_data_count a valid upper bound for the host
   * download loop, whose index space is the checkpoint index space.
   */
  pState->external_blocks = pState->pages;
  return 0;
}

/*
 * External sample-field writes.
 *
 * Each acquisition wake programs at most three 32-bit fields: the previous
 * sample's activity word, then the current sample's pressure and temperature.
 * A rest between program cycles lets the storage capacitor recharge, which is
 * the whole reason the fields are written separately rather than as one
 * twelve-byte record.
 */

/** @brief Recharge rest between external program cycles, in milliseconds. */
#ifndef UIUCTAG_WRITE_REST_MS
#define UIUCTAG_WRITE_REST_MS 20U
#endif

/* Public API contract documented in datalog.h. */
void dataLogWriteBegin(void)
{
  tagStorageWake(TAG_EXTERNAL_FLASH);
}

/* Public API contract documented in datalog.h. */
void dataLogWriteEnd(void)
{
  tagStorageSleep(TAG_EXTERNAL_FLASH);
}

/* Public API contract documented in datalog.h. */
enum LOGERR dataLogWriteField(uint32_t sample_index, uint32_t field_offset,
                              const void *word)
{
  uint8_t buffer[4];
  int count = (int)sizeof(buffer);
  uint32_t address = (sample_index * UIUCTAG_SAMPLE_SIZE) + field_offset;

  if ((address + sizeof(buffer)) > externalFlashSize())
    return LOGWRITE_FULL;

  memcpy(buffer, word, sizeof(buffer));

  if (!tagStorageWrite(TAG_EXTERNAL_FLASH, address, buffer, &count) ||
      count != (int)sizeof(buffer))
    return LOGWRITE_ERROR;

  /*
   * Sleep rather than spin: the rest exists to let the storage capacitor
   * recover, and burning MCU current through it would defeat the purpose.
   */
  stopMilliseconds(UIUCTAG_WRITE_REST_MS);

  return LOGWRITE_OK;
}

/* Public API contract documented in datalog.h. */
enum LOGERR writeDataHeader(t_DataHeader *head)
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
  if (head->vdd100 < 200)
    return LOGWRITE_BAT;
  else
    return LOGWRITE_OK;
}

//
// Generate monitor ack for data log request
//   Executed by the monitor thread
//

/**
 * @brief Populate and encode a monitor ACK for one UIUCTag log block.
 *
 * @details The response is the raw external block image plus the decoded
 *          checkpoint header, so the tag spends no energy converting units:
 *          pressure and temperature are already stored in engineering units and
 *          the host unpacks the packed samples with the shared helpers in
 *          uiuctag_log_format.h.
 *
 *          Trailing slots that were never written are trimmed from the payload
 *          so a partial final block returns only what it holds. Interior gaps
 *          are left in place, because slot position is what carries time: the
 *          host reads an unwritten field as NaN and skips it.
 *
 * @param[in] index Checkpoint index, one per external block.
 * @param[out] ack ACK message to fill. An index with no written checkpoint
 *                 yields Ack_Err_NODATA and no payload.
 * @return Encoded ACK length.
 *
 * @note Runs on the monitor thread at raised priority and clock speed, since
 *       formatting the response competes with the USB link timing.
 */
int data_logAck(int index, Ack *ack)
{
  int ret;
  t_DataHeader header;
  UIUCTagLog *data = &ack->payload.uiuctag_data_log;

  chThdSetPriority(HIGHPRIO);
  fast_msi();

  ack->err = Ack_Err_NODATA;
  ack->which_payload = 0;

  if ((index >= 0) && ((uint32_t)index < pState->pages) &&
      readDataHeader(index, &header) && (header.epoch != -1))
  {
    uint64_t byte_offset =
        (uint64_t)header.extern_log_block * DATALOG_BLOCK_BYTES;

    if ((byte_offset + DATALOG_BLOCK_BYTES) <= (uint64_t)externalFlashSize())
    {
      size_t used = DATALOG_BLOCK_BYTES;

      tagStorageWake(TAG_EXTERNAL_FLASH);
      tagStorageRead(TAG_EXTERNAL_FLASH, (uint32_t)byte_offset,
                     data->samples.bytes, DATALOG_BLOCK_BYTES);
      tagStorageSleep(TAG_EXTERNAL_FLASH);

      /* Trim trailing never-written slots; keep interior gaps in place. */
      while (used >= UIUCTAG_SAMPLE_SIZE)
      {
        t_UIUCTagSample sample;

        memcpy(&sample, &data->samples.bytes[used - UIUCTAG_SAMPLE_SIZE],
               sizeof(sample));
        if (!uiuctagSampleErased(&sample))
          break;
        used -= UIUCTAG_SAMPLE_SIZE;
      }

      ack->which_payload = Ack_uiuctag_data_log_tag;
      ack->err = Ack_Err_OK;
      data->epoch = header.epoch;
      data->voltage = header.vdd100 * 0.01f;
      data->samples.size = (pb_size_t)used;
    }
  }

  // encode the ack and return
  ret = encode_ack();
  slow_msi();
  chThdSetPriority(NORMALPRIO);
  return ret;
}
