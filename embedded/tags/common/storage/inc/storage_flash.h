/**
 * @file storage_flash.h
 * @brief Generic external flash API dispatched through TagStorageDevice ops.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_STORAGE_FLASH_H
#define TAG_STORAGE_FLASH_H

#include "storage_device.h"

/** @name Generic external flash API
 * Generic external-flash API used between the legacy Ex* callers and the
 * selected chip driver. Each active tag or family devices.c file provides
 * tagExternalFlash; this layer dispatches through its operation table.
 * @{
 */
extern const TagStorageDevice tagExternalFlash;

/**
 * @brief Return the erase-sector size for a storage device.
 *
 * @param[in] dev Storage device descriptor.
 * @return Sector size in bytes.
 */
int tagStorageSectorSize(const TagStorageDevice *dev);

/**
 * @brief Return the number of erase sectors on a storage device.
 *
 * @param[in] dev Storage device descriptor.
 * @return Sector count.
 */
int tagStorageSectorCount(const TagStorageDevice *dev);

/**
 * @brief Wake the external flash and begin its bus session.
 *
 * @param[in] dev Storage device descriptor.
 */
void tagStorageWake(const TagStorageDevice *dev);

/**
 * @brief Put the external flash to sleep and end its bus session.
 *
 * @param[in] dev Storage device descriptor.
 */
void tagStorageSleep(const TagStorageDevice *dev);

/**
 * @brief Apply bus-level sleep policy for the storage device.
 *
 * @param[in] dev Storage device descriptor.
 */
void tagStoragePrepareSleep(const TagStorageDevice *dev);

/**
 * @brief Prepare external flash for MCU standby when the state requires it.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] state Current tag state.
 */
void tagStoragePrepareStandby(const TagStorageDevice *dev, uint32_t state);

/**
 * @brief Apply standby pin policy for the external flash.
 *
 * @param[in] dev Storage device descriptor.
 */
void tagStorageApplyStandbyPins(const TagStorageDevice *dev);

/**
 * @brief Check the external flash JEDEC identity.
 *
 * @param[in] dev Storage device descriptor.
 * @return Chip-specific success value or -1 on mismatch.
 */
int tagStorageCheckID(const TagStorageDevice *dev);

/**
 * @brief Program bytes to external flash.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Flash byte address to program.
 * @param[in] buf Source buffer.
 * @param[in,out] cnt Requested byte count on entry, programmed byte count on return.
 * @return true when all requested bytes were programmed.
 */
bool tagStorageWrite(const TagStorageDevice *dev, uint32_t address,
                     uint8_t *buf, int *cnt);

/**
 * @brief Report whether the storage driver supports staged page programming.
 *
 * @param[in] dev Storage device descriptor.
 * @return true when program-load, random-load, and execute hooks are present.
 */
bool tagStorageSupportsProgramCache(const TagStorageDevice *dev);

/**
 * @brief Load the first byte range into a NAND page-program cache.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Byte address whose page and column identify the cache load.
 * @param[in] buf Source buffer.
 * @param[in] cnt Number of bytes to load.
 * @return true when the storage driver supports and accepts the cache load.
 */
bool tagStorageProgramLoad(const TagStorageDevice *dev, uint32_t address,
                           const uint8_t *buf, int cnt);

/**
 * @brief Update an existing NAND page-program cache byte range.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Byte address whose page and column identify the update.
 * @param[in] buf Source buffer.
 * @param[in] cnt Number of bytes to load.
 * @return true when the storage driver supports and accepts the cache update.
 */
bool tagStorageProgramLoadRandom(const TagStorageDevice *dev,
                                 uint32_t address,
                                 const uint8_t *buf, int cnt);

/**
 * @brief Commit the loaded NAND page-program cache to the flash array.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Byte address within the target logical page.
 * @return true when the storage driver supports and completes the execute.
 */
bool tagStorageProgramExecute(const TagStorageDevice *dev, uint32_t address);

/**
 * @brief Erase one external flash sector.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Address within the sector to erase.
 * @return true when the erase completed.
 */
bool tagStorageSectorErase(const TagStorageDevice *dev, uint32_t address);

/**
 * @brief Read bytes from external flash.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Flash byte address to read.
 * @param[out] buf Destination buffer.
 * @param[in] num Number of bytes to read.
 */
void tagStorageRead(const TagStorageDevice *dev, uint32_t address,
                    uint8_t *buf, int num);
/** @} */

#endif
