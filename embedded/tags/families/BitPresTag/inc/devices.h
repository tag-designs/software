#ifndef BITPRESTAG_DEVICES_H
#define BITPRESTAG_DEVICES_H

#include "ADXL362.h"
#include "lps27hhw.h"
#include "storage_flash.h"

extern const TagAdxl362Device tagBitPresTagAccelDevice;
extern const TagPressureDevice tagBitPresTagPressureDevice;
extern const TagStorageDevice tagExternalFlash;

#define TAG_ACCEL_DEVICE (&tagBitPresTagAccelDevice)
#define TAG_PRESSURE_DEVICE (&tagBitPresTagPressureDevice)
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)

static inline bool tagPressureTest(void)
{
  return lps27Test(TAG_PRESSURE_DEVICE);
}

#endif
