#ifndef TAG_STORAGE_DEVICE_H
#define TAG_STORAGE_DEVICE_H

#include "storage_spi.h"

#include <stdint.h>

/*
 * Board-facing description of an external flash device.
 *
 * Chip drivers own command formats, status polling, and timing rules. This
 * descriptor carries the tag-specific pieces needed to reach the chip: the SPI
 * transaction bus, optional board-level enable/disable hooks, and the geometry
 * used by higher-level logging code.
 */
typedef struct {
  const TagSpiBus *spi;
  void (*enable)(void);
  void (*disable)(void);
  uint32_t sector_size;
  uint32_t sector_count;
} TagStorageDevice;

static inline void tagStorageDeviceEnable(const TagStorageDevice *dev)
{
  if (dev->enable)
  {
    dev->enable();
  }
}

static inline void tagStorageDeviceDisable(const TagStorageDevice *dev)
{
  if (dev->disable)
  {
    dev->disable();
  }
}

#endif
