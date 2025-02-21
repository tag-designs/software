#include "app.h"
#include "lps.h"
#include "datalog.h"
#include <tag.pb.h>
#include "persistent.h"
#include "external_flash.h"

const int databuf_size = sizeof(t_DataLog);
static t_DataLog databuf NOINIT;
volatile int sectors_erased NOINIT;

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
  pState->external_blocks = pState->pages * DATALOG_SAMPLES*2;
  return 0;
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
  int addr = pState->external_blocks * 4;

  ExFlashPwrUp();
  for (int i = 0; i < num; i++)
  {
    cnt = 2;
    if (!ExFlashWrite(addr, (uint8_t *) &data[i], &cnt)) {
       //ExFlashPwrDown();
       //return LOGWRITE_ERROR;
       /* ignore error */
       /* what is right thing to do ? */
    }
    //SPI1->CR1 &= ~SPI_CR1_SPE;
    stopMilliseconds(true,2);
    //SPI1->CR1 |= SPI_CR1_SPE;
    addr += 2;
    //pState->external_blocks = addr/2;
  }
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
  if (head->vdd100[0] < 200)
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

  PresTagLog *data = &ack->payload.prestag_data_log;
  ack->err = Ack_Err_OK;

  // read data
  ExFlashPwrUp();
  ExFlashRead(sizeof(databuf)*index, (uint8_t *) &databuf, sizeof(databuf));
  ExFlashPwrDown();

  if (vddHeader[index].epoch != -1)
  {
    ack->which_payload = Ack_prestag_data_log_tag;
    data->epoch = vddHeader[index].epoch;
    data->voltage = vddHeader[index].vdd100[0] * 0.01f;
    data->temperature = vddHeader[index].vdd100[1] * 0.01f;
    data->data_count = 0;

    for (int j = 0; j < DATALOG_SAMPLES; j++) // loop over samples
    {
      // check for unwritten data
      if (databuf.data[j].pressure == -1)
        break;
      data->data[j].pressure = lpsPressure(databuf.data[j].pressure);
      data->data[j].temperature = lpsTemperature(databuf.data[j].temperature);
      data->data_count++;
    }
  }
  else
  {
    ack->which_payload = 0;
  }

  // encode the ack and return

  return encode_ack();
}


