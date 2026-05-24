#include "hal.h"

#include "core_runtime.h"
#include "core_sync.h"
#include "datalog.h"
#include "flash_internal.h"
#include "persistent.h"
#include "tag.pb.h"

#include <stdint.h>

extern int encode_ack(void);

int restoreLog(void)
{
  uint32_t flashend = (uint32_t)(0x8000000 +
                                 (*((uint16_t *)FLASHSIZE_BASE) * 1024));
  int i = 0;
  int logtime = 0;

  while (((uint32_t)&vddHeader[i] < flashend))
  {
    if (vddHeader[i].epoch >= 0)
    {
      logtime = vddHeader[i].epoch;
      i++;
    }
    else
    {
      break;
    }
  }

  pState->pages = i;
  return logtime;
}

enum LOGERR writeDataLog(uint64_t activity)
{
  t_DataHeader dlog;
  uint32_t *dlogptr = (uint32_t *)&dlog;
  uint32_t flashend = (uint32_t)(0x8000000 +
                                (*((uint16_t *)FLASHSIZE_BASE) * 1024));
  uint32_t *writeptr = (uint32_t *)&vddHeader[pState->pages++];

  dlog.vdd100 = pState->vdd100;
  dlog.temp10 = pState->temp10;
  dlog.epoch = timestamp;
  dlog.activity = activity;

  chSysLock();
  FLASH_Unlock();
  uint32_t flasherr = FLASH_Program_Array(writeptr, dlogptr, sizeof(dlog) / 4);
  FLASH_Lock();
  FLASH_Flush_Data_Cache();
  chSysUnlock();

  if ((((uint32_t)writeptr) + sizeof(dlog)) >= flashend)
    return LOGWRITE_FULL;
  if (flasherr)
    return LOGWRITE_ERROR;
  if (dlog.vdd100 < 200)
    return LOGWRITE_BAT;

  return LOGWRITE_OK;
}

int data_logAck(int index, Ack *ack)
{
  uint64_t *flashend =
      (uint64_t *)(0x8000000 + (*((uint16_t *)FLASHSIZE_BASE) * 1024));
  static const size_t max_data =
      sizeof(ack->payload.bittag_data_log.data) / sizeof(BitTagData);
  BitTagData *data = ack->payload.bittag_data_log.data;
  size_t count = 0;

  ack->err = Ack_Err_OK;
  ack->which_payload = Ack_bittag_data_log_tag;

  while ((count < max_data) && ((uint64_t *)&vddHeader[count + index] < flashend))
  {
    if ((uint32_t)vddHeader[index].epoch == 0xffffffff)
      break;

    data[count].epoch = vddHeader[index].epoch;
    data[count].temperature = vddHeader[index].temp10 * 0.1f;
    data[count].voltage = vddHeader[index].vdd100 * 0.01f;
    data[count].rawdata = vddHeader[index].activity;
    count++;
    index++;
  }

  ack->payload.bittag_data_log.data_count = count;
  return encode_ack();
}
