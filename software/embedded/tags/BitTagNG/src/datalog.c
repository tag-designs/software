#include "app.h"
#include "lps.h"
#include "datalog.h"
#include <tag.pb.h>
#include "persistent.h"
#include "external_flash.h"

const int databuf_size = sizeof(t_DataLog);
static t_DataLog databuf NOINIT;

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

// Recover pState from log

// Recover pState from log

int restoreLog()
{
  pState->pages = countInternalBlocks();
  pState->external_blocks = pState->pages * DATALOG_SAMPLES*2;
  return 0;
}


void eraseExternal()
{
  int32_t addr;
  int32_t size;

  // compute blocks

  int sectors; 
  restoreLog();

  // round up to full sector
  
  sectors = (pState->external_blocks*2+4095)/4096;
  pState->external_blocks = sectors*4096/2;

  ExFlashPwrUp();  
  size = ExCheckID();
  while (sectors>0) {
   
    addr = sectors * 4096;
    if (addr < size*1024) {
        ExFlashSectorErase(addr);
    }
    sectors -= 1;
    pState->external_blocks = sectors*4096/2;
  }
  ExFlashPwrDown();
}

// 
// Write data to external log
//

enum LOGERR writeDataLog(uint16_t *data, int num)
{
  if (pState->external_blocks*2 + num > EXT_FLASH_SIZE/2)
  {
    return LOGWRITE_FULL;
  }

  int cnt;
  int addr = pState->external_blocks * 2;

  ExFlashPwrUp();
  for (int i = 0; i < num; i++)
  {
    cnt = 2;
    if (!ExFlashWrite(addr, (uint8_t *) &data[i], &cnt)) {
       /* ignore error */
       /* what is right thing to do ? */
    }
    stopMilliseconds(true,2);
    addr += 2;
  }
  ExFlashPwrDown();
  pState->external_blocks += num;
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

  // read data
  ExFlashPwrUp();
  ExFlashRead(databuf_size*index, (uint8_t *) &databuf, databuf_size);
  ExFlashPwrDown();

  if (vddHeader[index].epoch != -1)
  {
    ack->which_payload = Ack_bittag_ng_data_log_tag;
    data->epoch = vddHeader[index].epoch;
    data->voltage = vddHeader[index].vdd100 * 0.01f;
    data->temperature = vddHeader[index].temp10 * 0.1f;
    data->activity_count = 0;

    for (int j = 0; j < DATALOG_SAMPLES; j++) // loop over samples
    {
      if (databuf.activity[j] == 0xffffffffu)
        break;
      data->activity[j] = databuf.activity[j];
      data->activity_count++;
    }
  }
  else
  {
    ack->which_payload = Ack_error_message_tag;
  }

  // encode the ack and return

  return encode_ack();
}


