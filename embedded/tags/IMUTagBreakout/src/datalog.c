/**
 * @file datalog.c
 * @brief IMUTagBreakout log storage, erase support, and monitor ACK export.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "app.h"
#include "datalog.h"
#include <string.h>
#include <tag.pb.h>
#include "devices.h"
#include "persistent.h"

const int databuf_size = DATALOG_SAMPLES * sizeof(t_DataLog);
static t_DataLog databuf[DATALOG_SAMPLES] NOINIT;
static volatile int sectors_erased NOINIT;

extern int encode_ack(void);

/**
 * @brief Erase one external sector if it contains programmed data.
 *
 * @param[in] sector Sector index to inspect.
 * @return true when the sector was erased.
 */
static bool eraseExternalSector(int sector){
  int32_t addr;
  uint8_t buf[256];

  // round up to full sector

  if (sector < 0 || sector >= tagStorageSectorCount(TAG_EXTERNAL_FLASH))
    return false;

  addr = sector * tagStorageSectorSize(TAG_EXTERNAL_FLASH);

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
  sectors_erased = 0;
  tagStorageWake(TAG_EXTERNAL_FLASH);
  for (int i = 0; i < tagStorageSectorCount(TAG_EXTERNAL_FLASH); i++) {
    // check them all
    eraseExternalSector(i);
    sectors_erased++;
    // allow monitor a chance
    if (i%8 == 7)
      chThdYield();  
  }
  tagStorageSleep(TAG_EXTERNAL_FLASH);
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
 * @brief Recover persistent log cursors from internal flash headers.
 *
 * @return 0 when recovery completes.
 */
int restoreLog(void)
{
  uint32_t end = 0x08000000 + (*((uint16_t *)FLASHSIZE_BASE)) * 1024;
  int i;
  for (i = 0; (uint32_t)&vddHeader[i] < end; i++)
  {
    if (vddHeader[i].epoch == -1)
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
  tagStorageWrite(TAG_EXTERNAL_FLASH, addr, (uint8_t *) data, &cnt);
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
  uint32_t flashend = (uint32_t)(0x8000000 +
                                (*((uint16_t *)FLASHSIZE_BASE) * 1024));

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

  uint32_t end = 0x08000000 + (*((uint16_t *)FLASHSIZE_BASE)) * 1024;

  // check for valid header

  if (index >= 0 && ((uint32_t)&vddHeader[index] < end) &&
      (vddHeader[index].epoch != -1))
  {

    // we have a valid header

    ack->which_payload = Ack_imu_data_log_tag;
    IMUTagLog *log = &ack->payload.imu_data_log;

    log->epoch = vddHeader[index].epoch;
    log->millisecond = vddHeader[index].millis;
    log->temperature = vddHeader[index].rawtemp * 0.01f;
    log->data_count = 0;

    // now read the data

    tagStorageWake(TAG_EXTERNAL_FLASH);
    tagStorageRead(TAG_EXTERNAL_FLASH, sizeof(databuf) * index,
                   (uint8_t *) &databuf, sizeof(databuf));
    tagStorageSleep(TAG_EXTERNAL_FLASH);

    for (int i = 0; i < DATALOG_SAMPLES; i++)
    {
      int cnt = log->data_count;
      log->data[cnt].pressure_raw = databuf[i].pressure;
      log->data[cnt].mx_raw = databuf[i].mx;
      log->data[cnt].my_raw = databuf[i].my;
      log->data[cnt].mz_raw = databuf[i].mz;
      log->data[cnt].data.size = sizeof(databuf[i].raw_data);
      memcpy(log->data[cnt].data.bytes, databuf[i].raw_data,
             sizeof(databuf[i].raw_data));
      log->data_count++;
    }

  }
  else
  {
    ack->which_payload = 0;
  }

  // encode the ack and return
  ret = encode_ack();
  chThdSetPriority(NORMALPRIO);
  return ret;
}
