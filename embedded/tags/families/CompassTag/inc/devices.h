#ifndef COMPASSTAG_DEVICES_H
#define COMPASSTAG_DEVICES_H

#include <stdbool.h>

#include "ak09940a.h"
#include "lis2du12.h"
#include "storage_flash.h"

extern const TagRegisterDevice tagCompassTagAccelDevice;
extern const TagMagDevice tagCompassTagMagDevice;
extern const TagStorageDevice tagExternalFlash;

#define TAG_ACCEL_DEVICE (&tagCompassTagAccelDevice)
#define TAG_MAG_DEVICE (&tagCompassTagMagDevice)
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)

void tagCompassMagResetAssert(void);
void tagCompassMagResetRelease(void);
bool tagCompassAccelWakeActive(void);

#endif
