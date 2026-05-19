#include "external_flash.h"
#include "rtc_api.h"
#include "storage_flash.h"
#include "tag.pb.h"

static bool tagStorageShouldSleepForStandby(uint32_t state)
{
  return state == IDLE ||
         state == ABORTED ||
         state == FINISHED ||
         state == EXCEPTION ||
         state == HIBERNATING;
}

int tagStorageSectorSize(const TagStorageDevice *dev)
{
  return dev->sector_size;
}

int tagStorageSectorCount(const TagStorageDevice *dev)
{
  return dev->sector_count;
}

void tagStorageWake(const TagStorageDevice *dev)
{
  dev->ops->wake(dev);
}

void tagStorageSleep(const TagStorageDevice *dev)
{
  dev->ops->sleep(dev);
}

void tagStoragePrepareSleep(const TagStorageDevice *dev)
{
  tagStorageDevicePrepareSleep(dev);
}

/*
 * TagDevice adapter callbacks for external flash.
 *
 * The storage layer still owns chip-level command dispatch. The generic device
 * table calls these wrappers with a void context so pwr.c can iterate over
 * board devices without knowing about TagStorageDevice internals.
 */
void tagStorageDeviceOn(const void *context)
{
  tagStorageWake((const TagStorageDevice *)context);
}

void tagStorageDeviceOff(const void *context)
{
  tagStorageSleep((const TagStorageDevice *)context);
}

void tagStorageDevicePrepareStandby(const void *context, uint32_t state)
{
  const TagStorageDevice *dev = (const TagStorageDevice *)context;

  if (!tagStorageShouldSleepForStandby(state))
  {
    return;
  }

  tagStorageWake(dev);
  stopMilliseconds(true, 1);
  tagStorageSleep(dev);
}

void tagStorageDeviceApplyStandbyPins(const void *context)
{
  /*
   * For SPI flash this bottoms out in tagSpiDevicePrepareSleep(), where the
   * SPI descriptor's sleep_policy maps pins to standby pullup/pulldown state.
   */
  tagStoragePrepareSleep((const TagStorageDevice *)context);
}

int tagStorageCheckID(const TagStorageDevice *dev)
{
  return dev->ops->check_id(dev);
}

bool tagStorageWrite(const TagStorageDevice *dev, uint32_t address,
                     uint8_t *buf, int *cnt)
{
  return dev->ops->write(dev, address, buf, cnt);
}

bool tagStorageSectorErase(const TagStorageDevice *dev, uint32_t address)
{
  return dev->ops->sector_erase(dev, address);
}

void tagStorageRead(const TagStorageDevice *dev, uint32_t address,
                    uint8_t *buf, int num)
{
  dev->ops->read(dev, address, buf, num);
}

int ExSectorSize(void)
{
  return tagStorageSectorSize(&tagExternalFlash);
}

int ExSectorCount(void)
{
  return tagStorageSectorCount(&tagExternalFlash);
}

void ExFlashPwrUp(void)
{
  tagStorageWake(&tagExternalFlash);
}

void ExFlashPwrDown(void)
{
  tagStorageSleep(&tagExternalFlash);
}

int ExCheckID(void)
{
  return tagStorageCheckID(&tagExternalFlash);
}

bool ExFlashWrite(uint32_t address, uint8_t *buf, int *cnt)
{
  return tagStorageWrite(&tagExternalFlash, address, buf, cnt);
}

bool ExFlashSectorErase(uint32_t address)
{
  return tagStorageSectorErase(&tagExternalFlash, address);
}

void ExFlashRead(uint32_t address, uint8_t *buf, int num)
{
  tagStorageRead(&tagExternalFlash, address, buf, num);
}
