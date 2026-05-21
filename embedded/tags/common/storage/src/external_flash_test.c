#include <stdbool.h>
#include <stdint.h>

#include "storage_flash.h"

bool __attribute__((weak)) tag_test_external_flash(void)
{
  tagStorageWake(&tagExternalFlash);
  bool result = tagStorageCheckID(&tagExternalFlash) > -1;
  tagStorageSleep(&tagExternalFlash);
  return result;
}
