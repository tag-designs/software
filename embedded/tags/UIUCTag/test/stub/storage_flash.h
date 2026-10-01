/* stub: the external-storage calls datalog.c makes; datalog_sim.c defines them. */
#ifndef STUB_STORAGE_FLASH_H
#define STUB_STORAGE_FLASH_H
#include <stdbool.h>
#include <stdint.h>
typedef struct { int unused; } TagStorageDevice;
extern const TagStorageDevice tagExternalFlash;
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)
int tagStorageSectorSize(const TagStorageDevice *dev);
int tagStorageSectorCount(const TagStorageDevice *dev);
void tagStorageWake(const TagStorageDevice *dev);
void tagStorageSleep(const TagStorageDevice *dev);
bool tagStorageWrite(const TagStorageDevice *dev, uint32_t address, uint8_t *buf, int *cnt);
bool tagStorageSectorErase(const TagStorageDevice *dev, uint32_t address);
void tagStorageRead(const TagStorageDevice *dev, uint32_t address, uint8_t *buf, int num);
#endif
