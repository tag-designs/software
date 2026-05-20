#ifndef COMPASSTAG_DEVICES_H
#define COMPASSTAG_DEVICES_H

#include "ak09940a.h"
#include "lis2du12.h"
#include "storage_flash.h"

extern const TagLis2du12Device tagCompassTagAccelDevice;
extern const TagMagDevice tagCompassTagMagDevice;
extern const TagStorageDevice tagExternalFlash;

#define TAG_ACCEL_DEVICE (&tagCompassTagAccelDevice)
#define TAG_MAG_DEVICE (&tagCompassTagMagDevice)
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)

void tagCompassMagResetAssert(void);
void tagCompassMagResetRelease(void);

#endif
