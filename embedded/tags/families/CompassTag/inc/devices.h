#ifndef COMPASSTAG_DEVICES_H
#define COMPASSTAG_DEVICES_H

#include "ak09940a.h"
#include "storage_flash.h"

extern const TagMagDevice tagCompassTagMagDevice;
extern const TagStorageDevice tagExternalFlash;

#define TAG_MAG_DEVICE (&tagCompassTagMagDevice)
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)

void tagCompassMagResetAssert(void);
void tagCompassMagResetRelease(void);

#endif
