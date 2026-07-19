/**
 * @file storage_device.h
 * @brief External storage descriptor and storage-to-bus lifecycle helpers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_STORAGE_DEVICE_H
#define TAG_STORAGE_DEVICE_H

#include "bus_device.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct TagStorageDevice TagStorageDevice;

/** @name Storage operation model
 * Chip-facing operations for an external flash device.
 *
 * The device descriptor below carries board-level wiring and geometry. This
 * table carries the chip-specific command implementation. Keeping the two
 * separate lets the generic storage API dispatch to AT25XE, MX25R, etc. without
 * teaching datalog code about chip command sets.
 * @{
 */
typedef struct {
  void (*wake)(const TagStorageDevice *dev);
  void (*sleep)(const TagStorageDevice *dev);
  int (*check_id)(const TagStorageDevice *dev);
  bool (*write)(const TagStorageDevice *dev, uint32_t address, uint8_t *buf,
                int *cnt);
  bool (*program_load)(const TagStorageDevice *dev, uint32_t address,
                       const uint8_t *buf, int cnt);
  bool (*program_load_random)(const TagStorageDevice *dev, uint32_t address,
                              const uint8_t *buf, int cnt);
  bool (*program_execute)(const TagStorageDevice *dev, uint32_t address);
  bool (*sector_erase)(const TagStorageDevice *dev, uint32_t address);
  void (*read)(const TagStorageDevice *dev, uint32_t address, uint8_t *buf,
               int num);
} TagStorageOps;
/** @} */

/** @name Storage device descriptor
 * Board-facing description of an external flash device.
 *
 * External storage is intentionally SPI-only at the chip-protocol level, but
 * still uses TagBusDevice for shared power/session/sleep handling.
 *
 * Chip drivers own command formats, status polling, and timing rules. This
 * descriptor carries the tag-specific pieces needed to reach the chip: the SPI
 * device descriptor and the geometry used by higher-level logging code.
 * @{
 */
struct TagStorageDevice {
  const TagStorageOps *ops;
  TagBusDevice bus;
  uint32_t sector_size;
  uint32_t sector_count;
};
/** @} */

/** @name Storage bus helpers
 * Inline helpers keep higher-level storage code coupled to storage descriptors
 * while delegating power/session/sleep details to the generic bus layer.
 * @{
 */
/**
 * @brief Return the SPI descriptor embedded in a storage device.
 *
 * @param[in] dev Storage device descriptor.
 * @return SPI device descriptor used by chip-level storage commands.
 */
static inline const TagSpiDevice *tagStorageSpiDevice(
    const TagStorageDevice *dev)
{
  return tagBusSpiDevice(&dev->bus);
}

/**
 * @brief Begin the storage device's bus session.
 *
 * @param[in] dev Storage device descriptor.
 */
static inline void tagStorageBusBegin(const TagStorageDevice *dev)
{
  tagBusBegin(&dev->bus);
}

/**
 * @brief End the storage device's bus session.
 *
 * @param[in] dev Storage device descriptor.
 */
static inline void tagStorageBusEnd(const TagStorageDevice *dev)
{
  tagBusEnd(&dev->bus);
}

/**
 * @brief Apply the storage device's bus sleep policy.
 *
 * @param[in] dev Storage device descriptor.
 */
static inline void tagStorageDevicePrepareSleep(const TagStorageDevice *dev)
{
  tagBusPrepareSleep(&dev->bus);
}
/** @} */

#endif
