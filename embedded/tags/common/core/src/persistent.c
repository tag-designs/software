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

#include "tag.pb.h"
#include "config.h"
#include "core_sync.h"
#include "custom.h"
#include "flash_internal.h"
#include "persistent.h"
#include "datalog.h"
#include "strings.h"
#include "assert.h"

#if defined(TAG_STM32U3_FLASH) && TAG_STM32U3_FLASH
/** Ensure U3 state markers match the 128-bit flash programming row. */
static_assert(sizeof(t_StateMarker) == 32, "sizeof(t_StateMarker) != 32!");
static_assert(sizeof(t_InternalDataHeader) == 16, "sizeof(t_InternalDataHeader) != 16!");
#else
/** Ensure state markers keep the flash-log layout expected by existing tags. */
static_assert(sizeof(t_StateMarker) == 24, "sizeof(t_StateMarker) != 24!");
#endif

#if defined(TAG_STM32U3_FLASH) && TAG_STM32U3_FLASH
#define TAG_FLASH_RECORD_ALIGN __attribute__((__aligned__(16)))
#else
#define TAG_FLASH_RECORD_ALIGN __attribute__((__aligned__(8)))
#endif

#include "adc.h"

static void tagStatusMeasure(uint16_t *vdd100, int16_t *temp10)
{
  adcVDD(vdd100, temp10);
#if defined(TAG_STATUS_FIXED_VDD100)
  *vdd100 = TAG_STATUS_FIXED_VDD100;
#endif
}

/** @name Persistent flash records
 * Objects placed in the persistent flash section and read directly after reset.
 * @{
 */
t_StateMarker sEpoch[sEPOCH_SIZE] __attribute__((section(".persistent")))
TAG_FLASH_RECORD_ALIGN __attribute__((no_reorder));
t_storedconfig sconfig TAG_FLASH_RECORD_ALIGN __attribute__((section(".persistent")))
TAG_FLASH_RECORD_ALIGN __attribute__((no_reorder));
t_InternalDataHeader vddHeader[256] __attribute__((section(".persistent")))
TAG_FLASH_RECORD_ALIGN __attribute__((no_reorder));
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
  uint32_t end = (uint32_t)&__persistent_end__;
  uint32_t start = ((uint32_t)(&__persistent_start__));
  uint32_t page_size = FLASH_PageSize();

  while (start + page_size <= end)
  {
    end -= page_size;
    uint64_t first_word;
    uint32_t readerr =
        FLASH_Read_DoubleWord_Checked((const uint64_t *)end, &first_word);
    if (!readerr && (((uint32_t)first_word) == 0xffffffff) && (end != start))
      continue;
    chSysLock();
    FLASH_Unlock();
    FLASH_PageEraseAddress(end);
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

void __attribute__((weak)) eraseExternalStart(void)
{
  eraseExternal();
}

bool __attribute__((weak)) eraseExternalNextSector(void)
{
  return false;
}

void __attribute__((weak)) eraseExternalFinish(void)
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
 * @brief Default total external sectors expected for the current erase.
 *
 * The plus-one encoding lets protobuf distinguish unsupported/unknown (0)
 * from a supported erase with zero sectors to process (1).
 *
 * @return 0 because this target does not report an erase total.
 */
int __attribute__((weak)) externalFlashSectorsToErasePlusOne(void)
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
  tagStatusMeasure(&vdd100, &temp10);
  size_t offset;

  // find next available log slot.
  for (offset = 0; offset < sEPOCH_SIZE; offset++)
  {
    t_StateMarker existing;
    if (FLASH_Read_Checked(&sEpoch[offset], &existing, sizeof(existing)))
      break;
    if (existing.epoch == -1)
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
