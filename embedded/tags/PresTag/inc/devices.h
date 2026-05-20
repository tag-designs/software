#ifndef PRESTAG_DEVICES_H
#define PRESTAG_DEVICES_H

#include "lps.h"
#include "storage_flash.h"

extern const TagPressureDevice tagPresTagPressureDevice;
extern const TagStorageDevice tagExternalFlash;

#define TAG_PRESSURE_DEVICE (&tagPresTagPressureDevice)
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)

static inline bool tagPressureTest(void)
{
  return lps27Test(TAG_PRESSURE_DEVICE);
}

#endif
