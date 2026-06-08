#include "hal.h"

#include "core_runtime.h"
#include "core_sync.h"
#include "datalog.h"
#include "flash_internal.h"
#include "persistent.h"
#include "tag.pb.h"

#include <stdbool.h>
#include <stdint.h>

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

int restoreLog(void)
{
  int i = 0;
  int logtime = 0;
  t_DataHeader header;

  while (readDataHeader(i, &header))
  {
    if (header.epoch >= 0)
    {
      logtime = header.epoch;
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
  uint32_t flashend = (uint32_t)&__persistent_end__;
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
  static const size_t max_data =
      sizeof(ack->payload.bittag_data_log.data) / sizeof(BitTagData);
  BitTagData *data = ack->payload.bittag_data_log.data;
  size_t count = 0;

  ack->err = Ack_Err_OK;
  ack->which_payload = Ack_bittag_data_log_tag;

  if (index < 0)
  {
    ack->payload.bittag_data_log.data_count = 0;
    return encode_ack();
  }

  while (count < max_data)
  {
    t_DataHeader header;
    if (!readDataHeader(index, &header))
      break;
    if ((uint32_t)header.epoch == 0xffffffff)
      break;

    data[count].epoch = header.epoch;
    data[count].temperature = header.temp10 * 0.1f;
    data[count].voltage = header.vdd100 * 0.01f;
    data[count].rawdata = header.activity;
    count++;
    index++;
  }

  ack->payload.bittag_data_log.data_count = count;
  return encode_ack();
}
