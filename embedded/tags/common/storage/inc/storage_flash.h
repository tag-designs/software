#ifndef TAG_STORAGE_FLASH_H
#define TAG_STORAGE_FLASH_H

#include "storage_device.h"

/*
 * Generic external-flash API used between the legacy Ex* callers and the
 * selected chip driver. Each flash module provides tagExternalFlash; this layer
 * dispatches through its operation table.
 */
extern const TagStorageDevice tagExternalFlash;

int tagStorageSectorSize(const TagStorageDevice *dev);
int tagStorageSectorCount(const TagStorageDevice *dev);
void tagStorageWake(const TagStorageDevice *dev);
void tagStorageSleep(const TagStorageDevice *dev);
void tagStoragePrepareSleep(const TagStorageDevice *dev);
int tagStorageCheckID(const TagStorageDevice *dev);
bool tagStorageWrite(const TagStorageDevice *dev, uint32_t address,
                     uint8_t *buf, int *cnt);
bool tagStorageSectorErase(const TagStorageDevice *dev, uint32_t address);
void tagStorageRead(const TagStorageDevice *dev, uint32_t address,
                    uint8_t *buf, int num);

#endif
