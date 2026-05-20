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

static inline float tagPressureValue(int16_t pressure)
{
  return lps27Pressure(pressure);
}

static inline float tagPressureTemperature(int16_t temperature)
{
  return lps27Temperature(temperature);
}

#endif
