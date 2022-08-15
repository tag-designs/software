#include "app.h"
#include "lps.h"
#include "datalog.h"
#include <tag.pb.h>
#include "persistent.h"
#include "external_flash.h"

const int databuf_size = sizeof(t_DataLog);
static t_DataLog databuf NOINIT;

extern int encode_ack(void);

// Recover pState from log

int restoreLog()
{
  uint32_t addr = 0;
  uint32_t next_addr = 0;
  int32_t buf;
  const uint32_t blocksize = sizeof(t_DataLog)*1024;

  ExFlashPwrUp();
  uint32_t size = ExCheckID() * 1024;

  // read epoch of block 0


  ExFlashRead(0,(uint8_t *) &buf, 4);

  if (buf == -1) // erased ?
  {
    pState->pages = 0;
    pState->external_blocks = 0;
    ExFlashPwrDown();
    return 0;
  }

  // search for end of written blocks
  // step size  1024*blocksize... 1*blocksize

  for (int i = 10; i > 0; i--) {
    uint32_t step = (1<<i)*blocksize;
    for ( next_addr = addr + step; 
          next_addr + blocksize <= size; 
          next_addr = addr + step) 
    {
      ExFlashRead(next_addr,(uint8_t *) &buf,4);
      if (buf == -1) {
        break;
      }
      addr = next_addr;
    }
  }
  ExFlashPwrDown();

  // treat last block found as full
  pState->pages = addr/blocksize;
  pState->external_blocks = (addr + blocksize)/2;
  return 0;
}

// 
// Write data to external log
//

enum LOGERR writeDataLog(uint16_t *data, int num)
{
  if (pState->external_blocks + num > EXT_FLASH_SIZE / 2)
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
       //ExFlashPwrDown();
       //return LOGWRITE_ERROR;
       /* ignore error */
       /* what is right thing to do ? */
    }
    //SPI1->CR1 &= ~SPI_CR1_SPE;
    stopMilliseconds(true,2);
    //SPI1->CR1 |= SPI_CR1_SPE;
    addr += 2;
    pState->external_blocks = addr/2;
  }
  ExFlashPwrDown();
  
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
  ExFlashPwrUp();
  ExFlashRead(sizeof(databuf)*index, (uint8_t *) &databuf, sizeof(databuf));
  ExFlashPwrDown();

  if (databuf.epoch != -1)
  {
    ack->which_payload = Ack_prestag_data_log_tag;
    data->epoch = databuf.epoch;
    data->voltage = databuf.vdd100 / 100.0f;
    data->temperature = databuf.temp10 /10.0f;
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


