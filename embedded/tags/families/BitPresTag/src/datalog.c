/**
 * @file datalog.c
 * @brief BitPresTag log storage, erase support, and monitor ACK export.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "app.h"
#include "datalog.h"
#include "devices.h"
#include "lps27hhw.h"
#include "storage_flash.h"
#include <tag.pb.h>
#include "persistent.h"
#include "custom.h"

const int databuf_size = sizeof(t_DataLog);
static t_DataLog databuf NOINIT;
static volatile int sectors_erased NOINIT;

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



extern int encode_ack(void);

/**
 * @brief Count valid internal flash headers after reset.
 *
 * @return Number of written internal header blocks.
 */
static int countInternalBlocks(void){
  uint32_t end = 0x08000000 + (*((uint16_t *)FLASHSIZE_BASE)) * 1024;
  uint32_t start = ((uint32_t)(&__persistent_start__));
  int count = 0;
  while (start < end) {

      if (((uint32_t *) start)[0] == 0xffffffff)
          break;
      count++;
      start += 8;
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
  pState->pages = countInternalBlocks();
  pState->external_blocks = pState->pages * DATALOG_SAMPLES*2;
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
  if ((pState->external_blocks * 2) + (uint32_t)num > flash_capacity / 2)
  {
    return LOGWRITE_FULL;
  }

  int cnt = num*2;
  int addr = pState->external_blocks * cnt;

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
 * @brief Populate and encode a monitor ACK for one BitPresTag log page.
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
  BitPresTagLog *data = &ack->payload.bitprestag_data_log;
  ack->err = Ack_Err_OK;
  
  // read data
  tagStorageWake(TAG_EXTERNAL_FLASH);
  tagStorageRead(TAG_EXTERNAL_FLASH, sizeof(databuf) * index,
                 (uint8_t *)&databuf, sizeof(databuf));
  tagStorageSleep(TAG_EXTERNAL_FLASH);

  if (vddHeader[index].epoch != -1)
  {
    ack->which_payload = Ack_bitprestag_data_log_tag;
    data->epoch = vddHeader[index].epoch;
    data->voltage = vddHeader[index].vdd100[0] * 0.01f;
    data->temperature = vddHeader[index].vdd100[1] * 0.01f;
    data->data_count = 0;

    for (int j = 0; j < DATALOG_SAMPLES; j++) // loop over samples
    {
      if (databuf.data[j].pressure == -1)
        break;
      data->data[j].activity = databuf.data[j].activity;
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

  // encode the ack and return
  ret = encode_ack();
  slow_msi();
  chThdSetPriority(NORMALPRIO);
  return ret;
}
