/***************************************************************
 *    Persistent (in Flash) state
 *
 *       In a dedicated flash page allocated in section .persistent
 *       see ../common/STM32L432xC.ld
 *
 *       sEpoch :   state history
 *
 *       to do  :  should write adxl configuration here to be
 *                 retreived by the run state
 *
 *              :  log negative events ???
 *
 ****************************************************************/
#include <stdint.h>
#include "hal.h"

#include "app.h"
#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
#include "datalog.h"
#include "monitor.h"
#include "external_flash.h"

// Test data structure sizes !

CASSERT(sizeof(t_StateMarker) == 24);

// Data held in flash

// extern uint32_t __persistent_start__;

t_StateMarker sEpoch[sEPOCH_SIZE] __attribute__((section(".persistent")))
__attribute__((__aligned__(8))) __attribute__((no_reorder));
t_storedconfig sconfig __attribute__((__aligned__(8))) __attribute__((section(".persistent")))
__attribute__((__aligned__(8))) __attribute__((no_reorder));
t_DataHeader vddHeader[256] __attribute__((section(".persistent")))
__attribute__((__aligned__(8))) __attribute__((no_reorder));

// erase persistent area of flash



void erasePersistent(void)
{
  uint32_t end = 0x08000000 + (*((uint16_t *)FLASHSIZE_BASE)) * 1024;
  uint32_t start = ((uint32_t)(&__persistent_start__));

  while (start + 2048 <= end)
  {
    end -= 2048;
    if ((((uint32_t *)end)[0] == 0xffffffff) && (end != start))
      continue;
    chSysLock();
    FLASH_Unlock();
    FLASH_PageErase((end - 0x8000000) / 2048);
    FLASH_Lock();
    FLASH_Flush_Data_Cache();
    chSysUnlock();
  }
  pState->pages = 0;
}

// Erase external



void recordState(State_Event reason)
{
  t_StateMarker marker;
  bzero(&marker, sizeof(marker));
  uint16_t vdd100 = 0;
  int16_t temp10 = 0;
  adcVDD(&vdd100, &temp10);
  size_t offset;

  // find next available log slot.
  for (offset = 0; offset < sEPOCH_SIZE; offset++)
  {
    if (sEpoch[offset].epoch == -1)
      break;
  }

  if (offset >= sEPOCH_SIZE)
    return;

  marker.epoch = timestamp;
  marker.state = pState->state;
  marker.internal_pages = pState->pages;
  marker.external_pages = pState->external_blocks;
  marker.vdd100 = vdd100;
  marker.temp10 = temp10;
  marker.reason = reason;

  chSysLock();
  FLASH_Unlock();
  FLASH_Program_Array((uint32_t *)&sEpoch[offset], (uint32_t *)&marker, sizeof(marker) / 4);
  FLASH_Lock();
  FLASH_Flush_Data_Cache();
  chSysUnlock();
}
