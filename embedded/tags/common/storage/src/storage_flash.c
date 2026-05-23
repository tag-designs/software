/**
 * @file storage_flash.c
 * @brief Generic external flash dispatch and standby preparation helpers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "rtc_api.h"
#include "storage_flash.h"
#include "tag.pb.h"

/** @name Storage standby policy
 * Standby helpers decide whether the current state should leave external flash
 * asleep and bias its pins for low leakage.
 * @{
 */
/**
 * @brief Decide whether storage should be put to sleep before standby.
 *
 * @param[in] state Current tag state.
 * @return true when the state is idle or otherwise terminal/inactive.
 */
static bool tagStorageShouldSleepForStandby(uint32_t state)
{
  return state == IDLE ||
         state == ABORTED ||
         state == FINISHED ||
         state == EXCEPTION ||
         state == HIBERNATING;
}
/** @} */

/** @name Generic external flash API
 * Thin dispatch layer used by datalog and legacy Ex* callers.
 * @{
 */
/**
 * @brief Return the erase-sector size for a storage device.
 *
 * @param[in] dev Storage device descriptor.
 * @return Sector size in bytes.
 */
int tagStorageSectorSize(const TagStorageDevice *dev)
{
  return dev->sector_size;
}

/**
 * @brief Return the number of erase sectors on a storage device.
 *
 * @param[in] dev Storage device descriptor.
 * @return Sector count.
 */
int tagStorageSectorCount(const TagStorageDevice *dev)
{
  return dev->sector_count;
}

/**
 * @brief Wake the external flash and begin its bus session.
 *
 * @param[in] dev Storage device descriptor.
 */
void tagStorageWake(const TagStorageDevice *dev)
{
  dev->ops->wake(dev);
}

/**
 * @brief Put the external flash to sleep and end its bus session.
 *
 * @param[in] dev Storage device descriptor.
 */
void tagStorageSleep(const TagStorageDevice *dev)
{
  dev->ops->sleep(dev);
}

/**
 * @brief Apply bus-level sleep policy for the storage device.
 *
 * @param[in] dev Storage device descriptor.
 */
void tagStoragePrepareSleep(const TagStorageDevice *dev)
{
  tagStorageDevicePrepareSleep(dev);
}

/**
 * @brief Prepare external flash for MCU standby when the state requires it.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] state Current tag state.
 */
void tagStoragePrepareStandby(const TagStorageDevice *dev, uint32_t state)
{
  if (!tagStorageShouldSleepForStandby(state))
  {
    return;
  }

  tagStorageWake(dev);
  stopMilliseconds(1);
  tagStorageSleep(dev);
}

/**
 * @brief Apply standby pin policy for the external flash.
 *
 * @param[in] dev Storage device descriptor.
 */
void tagStorageApplyStandbyPins(const TagStorageDevice *dev)
{
  tagStoragePrepareSleep(dev);
}

/**
 * @brief Check the external flash JEDEC identity.
 *
 * @param[in] dev Storage device descriptor.
 * @return Chip-specific success value or -1 on mismatch.
 */
int tagStorageCheckID(const TagStorageDevice *dev)
{
  return dev->ops->check_id(dev);
}

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
                     uint8_t *buf, int *cnt)
{
  return dev->ops->write(dev, address, buf, cnt);
}

/**
 * @brief Erase one external flash sector.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Address within the sector to erase.
 * @return true when the erase completed.
 */
bool tagStorageSectorErase(const TagStorageDevice *dev, uint32_t address)
{
  return dev->ops->sector_erase(dev, address);
}

/**
 * @brief Read bytes from external flash.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Flash byte address to read.
 * @param[out] buf Destination buffer.
 * @param[in] num Number of bytes to read.
 */
void tagStorageRead(const TagStorageDevice *dev, uint32_t address,
                    uint8_t *buf, int num)
{
  dev->ops->read(dev, address, buf, num);
}
/** @} */
