#ifndef STUB_STORAGE_FLASH_H
#define STUB_STORAGE_FLASH_H
#include <stdbool.h>
#include <stdint.h>
#include "devices.h"
int tagStorageSectorSize(const TagStorageDevice *dev);
int tagStorageSectorCount(const TagStorageDevice *dev);
void tagStorageWake(const TagStorageDevice *dev);
void tagStorageSleep(const TagStorageDevice *dev);
bool tagStorageWrite(const TagStorageDevice *dev, uint32_t address, uint8_t *buf, int *cnt);
bool tagStorageSectorErase(const TagStorageDevice *dev, uint32_t address);
void tagStorageRead(const TagStorageDevice *dev, uint32_t address, uint8_t *buf, int num);
#endif
