#ifndef TAG_STORAGE_DEVICE_H
#define TAG_STORAGE_DEVICE_H

#include "bus_device.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct TagStorageDevice TagStorageDevice;

/*
 * Chip-facing operations for an external flash device.
 *
 * The device descriptor below carries board-level wiring and geometry. This
 * table carries the chip-specific command implementation. Keeping the two
 * separate lets the generic storage API dispatch to AT25XE, MX25R, etc. without
 * teaching datalog code about chip command sets.
 */
typedef struct {
  void (*wake)(const TagStorageDevice *dev);
  void (*sleep)(const TagStorageDevice *dev);
  int (*check_id)(const TagStorageDevice *dev);
  bool (*write)(const TagStorageDevice *dev, uint32_t address, uint8_t *buf,
                int *cnt);
  bool (*sector_erase)(const TagStorageDevice *dev, uint32_t address);
  void (*read)(const TagStorageDevice *dev, uint32_t address, uint8_t *buf,
               int num);
} TagStorageOps;

/*
 * Board-facing description of an external flash device.
 *
 * External storage is intentionally SPI-only at the chip-protocol level, but
 * still uses TagBusDevice for shared power/session/sleep handling.
 *
 * Chip drivers own command formats, status polling, and timing rules. This
 * descriptor carries the tag-specific pieces needed to reach the chip: the SPI
 * device descriptor and the geometry used by higher-level logging code.
 */
struct TagStorageDevice {
  const TagStorageOps *ops;
  TagBusDevice bus;
  uint32_t sector_size;
  uint32_t sector_count;
};

static inline const TagSpiDevice *tagStorageSpiDevice(
    const TagStorageDevice *dev)
{
  return tagBusSpiDevice(&dev->bus);
}

static inline void tagStorageBusBegin(const TagStorageDevice *dev)
{
  tagBusBegin(&dev->bus);
}

static inline void tagStorageBusEnd(const TagStorageDevice *dev)
{
  tagBusEnd(&dev->bus);
}

static inline void tagStorageDevicePrepareSleep(const TagStorageDevice *dev)
{
  tagBusPrepareSleep(&dev->bus);
}

#endif
