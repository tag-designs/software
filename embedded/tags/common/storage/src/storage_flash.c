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

void tagStorageApplyStandbyPins(const TagStorageDevice *dev)
{
  /*
   * For SPI flash this bottoms out in tagSpiDevicePrepareSleep(), where the
   * SPI descriptor's sleep_policy maps pins to standby pullup/pulldown state.
   */
  tagStoragePrepareSleep(dev);
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
