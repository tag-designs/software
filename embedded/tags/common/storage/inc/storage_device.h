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

/**
 * @typedef TagStorageDevice
 * @brief Opaque external-storage device descriptor used by storage APIs.
 */
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
/**
 * @struct TagStorageOps
 * @brief Chip-specific command table for a storage device.
 *
 * @details Hooks may be NULL only when the generic dispatcher explicitly
 *          treats the operation as optional. Program-cache hooks are optional
 *          because NOR parts do not implement NAND-style staged page
 *          programming; identity, erase, read, and normal write hooks are
 *          required for active datalog storage.
 */
typedef struct {
  void (*wake)(const TagStorageDevice *dev);  ///< Prepare the chip/bus for commands.
  void (*sleep)(const TagStorageDevice *dev); ///< Quiesce the chip/bus after commands.
  int (*check_id)(const TagStorageDevice *dev); ///< Verify chip identity.
  bool (*write)(const TagStorageDevice *dev, uint32_t address, uint8_t *buf,
                int *cnt); ///< Program bytes and report completed count.
  bool (*program_load)(const TagStorageDevice *dev, uint32_t address,
                       const uint8_t *buf, int cnt); ///< Start cache load.
  bool (*program_load_random)(const TagStorageDevice *dev, uint32_t address,
                              const uint8_t *buf, int cnt); ///< Update cache.
  bool (*program_execute)(const TagStorageDevice *dev, uint32_t address); ///< Commit cache.
  bool (*sector_erase)(const TagStorageDevice *dev, uint32_t address); ///< Erase one sector.
  void (*read)(const TagStorageDevice *dev, uint32_t address, uint8_t *buf,
               int num); ///< Read bytes from the flash array.
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
/**
 * @struct TagStorageDevice
 * @brief Board binding and geometry for an external flash device.
 */
struct TagStorageDevice {
  const TagStorageOps *ops; ///< Chip-specific operation table.
  TagBusDevice bus;         ///< SPI bus binding and low-power policy.
  uint32_t sector_size;     ///< Erase-sector size in bytes.
  uint32_t sector_count;    ///< Number of erase sectors in the array.
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
