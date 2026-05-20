#ifndef TAG_STORAGE_DEVICE_H
#define TAG_STORAGE_DEVICE_H

#include "power.h"
#include "storage_spi.h"

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
 * Chip drivers own command formats, status polling, and timing rules. This
 * descriptor carries the tag-specific pieces needed to reach the chip: the SPI
 * device descriptor, optional board-level enable/disable hooks, and the geometry
 * used by higher-level logging code.
 */
struct TagStorageDevice {
  const TagStorageOps *ops;
  const TagSpiDevice *spi;
  void (*enable)(void);
  void (*disable)(void);
  uint32_t sector_size;
  uint32_t sector_count;
};

static inline void tagStorageDeviceEnable(const TagStorageDevice *dev)
{
  if (dev->enable)
  {
    dev->enable();
    return;
  }

  if (dev->spi)
  {
    tagSpiBusBegin(dev->spi);
  }
}

static inline void tagStorageDeviceDisable(const TagStorageDevice *dev)
{
  if (dev->disable)
  {
    dev->disable();
    return;
  }

  if (dev->spi)
  {
    tagSpiBusEnd(dev->spi);
  }
}

static inline void tagStorageDevicePrepareSleep(const TagStorageDevice *dev)
{
  if (dev->spi)
  {
    tagSpiDevicePrepareSleep(dev->spi);
  }
}

#endif
