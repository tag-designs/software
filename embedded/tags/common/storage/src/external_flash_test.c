/**
 * @file external_flash_test.c
 * @brief Shared external flash self-test implementation.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include <stdbool.h>
#include <stdint.h>

#include "storage_flash.h"
#include "test_support.h"

#if defined(TAG_FLASH_AT25XE) && TAG_FLASH_AT25XE
#define TAG_EXTERNAL_FLASH_FAILED AT25XE_FAILED
#elif defined(TAG_FLASH_MX25L) && TAG_FLASH_MX25L
#define TAG_EXTERNAL_FLASH_FAILED MX25L_FAILED
#elif defined(TAG_FLASH_MX25R) && TAG_FLASH_MX25R
#define TAG_EXTERNAL_FLASH_FAILED MX25R_FAILED
#else
#define TAG_EXTERNAL_FLASH_FAILED EXT_FLASH_FAILED
#endif

/** @name External flash self-test
 * Test helper used by tag self-test tables to verify configured external flash
 * can wake and return the expected identity.
 * @{
 */
/**
 * @brief Wake external flash and check its chip identity.
 *
 * @param[in] context Optional TagStorageDevice descriptor.
 * @return ALL_PASSED when the configured flash responds with a valid identity,
 * otherwise the selected flash device's failure result.
 */
TestResult __attribute__((weak)) tag_test_external_flash(const void *context)
{
  const TagStorageDevice *device = context ? context : &tagExternalFlash;

  tagStorageWake(device);
  bool result = tagStorageCheckID(device) > -1;
  tagStorageSleep(device);
  return result ? ALL_PASSED : TAG_EXTERNAL_FLASH_FAILED;
}
/** @} */
