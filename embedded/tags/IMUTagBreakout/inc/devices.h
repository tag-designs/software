#ifndef IMUTAGBREAKOUT_DEVICES_H
#define IMUTAGBREAKOUT_DEVICES_H

#include "lps.h"
#include "storage_flash.h"

extern const TagPressureDevice tagImuTagPressureDevice;
extern const TagStorageDevice tagExternalFlash;

#define TAG_PRESSURE_DEVICE (&tagImuTagPressureDevice)
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)

#endif
