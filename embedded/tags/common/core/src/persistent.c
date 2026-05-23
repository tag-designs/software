/**
 * @file persistent.c
 * @brief Internal flash persistence for configuration and state history.
 * @author tag firmware authors
 * @date 2026-05-23
 *
 * The persistent region is allocated in section .persistent by the tag linker
 * script. Core code records state history here while tag/family storage code
 * may provide external log erasure and recovery hooks.
 */
#include <stdint.h>
#include <stdbool.h>
#include "hal.h"

#include "adc.h"
#include "tag.pb.h"
#include "config.h"
#include "core_sync.h"
#include "custom.h"
#include "flash_internal.h"
#include "persistent.h"
#include "datalog.h"
#include "strings.h"
#include "assert.h"

/** Ensure state markers keep the flash-log layout expected by existing tags. */
static_assert(sizeof(t_StateMarker) == 24, "sizeof(t_StateMarker) != 24!");

/** @name Persistent flash records
 * Objects placed in the persistent flash section and read directly after reset.
 * @{
 */
t_StateMarker sEpoch[sEPOCH_SIZE] __attribute__((section(".persistent")))
__attribute__((__aligned__(8))) __attribute__((no_reorder));
t_storedconfig sconfig __attribute__((__aligned__(8))) __attribute__((section(".persistent")))
__attribute__((__aligned__(8))) __attribute__((no_reorder));
t_DataHeader vddHeader[256] __attribute__((section(".persistent")))
__attribute__((__aligned__(8))) __attribute__((no_reorder));
/** @} */

/** @name Internal and external persistence operations
 * Operations used by monitor commands and reset handling to erase persistent
 * regions and append state markers without knowing tag-specific storage.
 * @{
 */
/**
 * @brief Erase the internal flash region reserved for persistent records.
 */
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

/**
 * @brief Default no-op external flash erase for tags without external storage.
 *
 * Tags with external flash provide these from their datalog.c file. The weak
 * defaults let core reset code call the external erase path unconditionally on
 * tags that only have internal persistent state.
 */
void __attribute__((weak)) eraseExternal(void)
{
}

/**
 * @brief Default no-op block erase for tags without external storage.
 */
void __attribute__((weak)) eraseExternalBlock(void)
{
}

/**
 * @brief Default external storage size for tags without external storage.
 *
 * @return 0 because no external storage is present.
 */
uint32_t __attribute__((weak)) externalFlashSize(void)
{
  return 0;
}

/**
 * @brief Default erased-sector count for tags without external storage.
 *
 * @return 0 because no external storage is present.
 */
int __attribute__((weak)) externalFlashSectorsErased(void)
{
  return 0;
}

/**
 * @brief Append a state transition marker to internal flash.
 *
 * @param[in] reason Event that caused the state transition.
 */
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
/** @} */
