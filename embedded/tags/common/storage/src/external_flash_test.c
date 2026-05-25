/**
 * @file external_flash_test.c
 * @brief Shared external flash self-test implementation.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include <stdbool.h>
#include <stdint.h>

#include "storage_flash.h"

/** @name External flash self-test
 * Test helper used by tag self-test tables to verify configured external flash
 * can wake and return the expected identity.
 * @{
 */
/**
 * @brief Wake external flash and check its chip identity.
 *
 * @param[in] context Optional TagStorageDevice descriptor.
 * @return true when the configured flash responds with a valid identity.
 */
bool __attribute__((weak)) tag_test_external_flash(const void *context)
{
  const TagStorageDevice *device = context ? context : &tagExternalFlash;

  tagStorageWake(device);
  bool result = tagStorageCheckID(device) > -1;
  tagStorageSleep(device);
  return result;
}
/** @} */
