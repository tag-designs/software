#include "app.h"
#include "datalog.h"
#include <tag.pb.h>
#include "persistent.h"
#include "external_flash.h"

const int databuf_size = sizeof(t_DataLog);
static t_DataLog databuf NOINIT;
volatile int sectors_erased NOINIT;

// P137 in RM0394

static void fast_msi(void){
  // change to 24Mhz doesn't require VOS change
  // Adjust Wait States

  FLASH->ACR = (FLASH->ACR & ~(7)) | 3;

  // Change MSI frequency P 197 RM0394

  RCC->CR = (RCC->CR & ~(15<<4)) | (9<<4);

  // Change TIM2 Prescaler

  STM32_ST_TIM->PSC = STM32_ST_TIM->PSC * 12;

}


static void slow_msi(void){

 
   // Restore MSI frequency P 197 RM0394

   RCC->CR = (RCC->CR & ~(15<<4)) | (5<<4);

  // Adjust Wait States

  FLASH->ACR = FLASH->ACR & ~(7);

  // Restore TIM2 Prescaler

  STM32_ST_TIM->PSC = STM32_ST_TIM->PSC/12;

}



extern int encode_ack(void);

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

static bool eraseExternalSector(int sector){
  int32_t addr;
  uint8_t buf[256];

  // round up to full sector

  if (sector < 0 || sector >= EXT_FLASH_SIZE/4096)
    return false;

  addr = sector*4096;

  // read a buffer
  ExFlashRead(addr, buf, 256);
  for (int i = 0; i < 256; i++) {
      if (buf[i] != 255) {
        ExFlashSectorErase(addr);
        return true;
      }
  }
  return false;
}

void eraseExternal()
{
  sectors_erased = 0;
  ExFlashPwrUp();  
  for (int i = 0; i < EXT_FLASH_SIZE/4096; i++) {
    // check them all
    eraseExternalSector(i);
    sectors_erased++;
    // allow monitor a chance
    if (i%8 == 7)
      chThdYield();  
  }
  ExFlashPwrDown();
  pState->external_blocks = 0;
  sectors_erased = 0;
}

// Recover pState from log

int restoreLog(void)
{
  pState->pages = countInternalBlocks();
  pState->external_blocks = pState->pages * DATALOG_SAMPLES*sizeof(t_DataLog)/2;
  return 0;
}

// 
// Write data to external log
//

enum LOGERR writeDataLog(uint16_t *data, int num)
{
  if (pState->external_blocks + num > EXT_FLASH_SIZE/2)
  {
    return LOGWRITE_FULL;
  }

  int cnt = num*2;
  int addr = pState->external_blocks * 2;

  ExFlashPwrUp();
  ExFlashWrite(addr, (uint8_t *) data, &cnt);
  ExFlashPwrDown();
  
  return LOGWRITE_OK;
}

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
  int ret;
  chThdSetPriority(HIGHPRIO);
  fast_msi();
  CompassTagLog *data = &ack->payload.compasstag_data_log;
  ack->err = Ack_Err_OK;
  
  // read data
  ExFlashPwrUp();
  ExFlashRead(sizeof(databuf)*index, (uint8_t *) &databuf, sizeof(databuf));
  ExFlashPwrDown();

  if (vddHeader[index].epoch != -1)
  {
    ack->which_payload = Ack_compasstag_data_log_tag;
    data->epoch = vddHeader[index].epoch;
    data->voltage = vddHeader[index].vdd100 * 0.01f;
    data->temperature = vddHeader[index].temp10 * 0.1f;
    data->data_count = 0;

    // For each databuf[i]
    //.   check if activity != 0xffff (erased), finished if erased
    //.   for i = 1..4
    //.       write activity. [extract bits and divide by 16.0f]
    //.       write a[x,y,z], m[x,y,z]. -- convert to float with appropriate constants
    //.       increment data_count;

    // loop over records
    for (int i = 0; i < DATALOG_SAMPLES; i++){
      uint16_t activity = databuf.data[i].activity;
      // check for erased activity
      if (activity == 0xffff) continue;
      for (int j = 0; j < 4; j++){
        int cnt = data->data_count;
        // extract activity bits and convert to percentage
        data->data[cnt].activity = ((activity >> (j*4)) & 0xf) * 100.0f/16.0f;
        // get accelerometer data 
        data->data[cnt].ax = databuf.data[i].sensors[j].ax * 0.976f;
        data->data[cnt].ay = databuf.data[i].sensors[j].ay * 0.976f;
        data->data[cnt].az = databuf.data[i].sensors[j].az * 0.976f;
        // get magnetometer data
        data->data[cnt].mx = databuf.data[i].sensors[j].mx * 0.04f;
        data->data[cnt].my = databuf.data[i].sensors[j].my * 0.04f;
        data->data[cnt].mz = databuf.data[i].sensors[j].mz * 0.04f;
        data->data_count++;
      }
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


