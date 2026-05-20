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

static inline float tagPressureValue(int16_t pressure)
{
  return lps27Pressure(pressure);
}

static inline float tagPressureTemperature(int16_t temperature)
{
  return lps27Temperature(temperature);
}

static inline bool tagExternalFlashTest(void)
{
  tagStorageWake(TAG_EXTERNAL_FLASH);
  bool result = tagStorageCheckID(TAG_EXTERNAL_FLASH) > -1;
  tagStorageSleep(TAG_EXTERNAL_FLASH);
  return result;
}

#endif
