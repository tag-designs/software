#ifndef BITPRESTAG_DEVICES_H
#define BITPRESTAG_DEVICES_H

#include "lps.h"
#include "storage_flash.h"

extern const TagPressureDevice tagBitPresTagPressureDevice;
extern const TagStorageDevice tagExternalFlash;

#define TAG_PRESSURE_DEVICE (&tagBitPresTagPressureDevice)
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)

static inline bool tagPressureTest(void)
{
  return lps27Test(TAG_PRESSURE_DEVICE);
}

#endif
