#include "device.h"

/*
 * Weak empty device table.
 *
 * Tags that have been moved to descriptor-driven device handling provide a
 * strong table from their devices.c file. Older tags can continue without one
 * while the migration proceeds.
 */
__attribute__((weak)) const TagDeviceTableEntry tagDeviceTable[1] = {
    {0, 0},
};

__attribute__((weak)) const size_t tagDeviceCount = 0;

void tagDeviceOn(const TagDevice *device)
{
  if (device && device->on)
  {
    device->on(device->context);
  }
}

void tagDeviceOff(const TagDevice *device)
{
  if (device && device->off)
  {
    device->off(device->context);
  }
}

void tagDevicePrepareStandby(const TagDevice *device, uint32_t state)
{
  if (device && device->prepare_standby)
  {
    device->prepare_standby(device->context, state);
  }
}

void tagDeviceApplyStandbyPulls(const TagDevice *device)
{
  if (device && device->apply_standby_pulls)
  {
    device->apply_standby_pulls(device->context);
  }
}

void tagDeviceTablePrepareStandby(uint32_t state)
{
  for (size_t i = 0; i < tagDeviceCount; i++)
  {
    if (tagDeviceTable[i].flags & TAG_DEVICE_PREPARE_STANDBY)
    {
      tagDevicePrepareStandby(tagDeviceTable[i].device, state);
    }
  }
}

void tagDeviceTableApplyStandbyPulls(void)
{
  for (size_t i = 0; i < tagDeviceCount; i++)
  {
    if (tagDeviceTable[i].flags & TAG_DEVICE_APPLY_STANDBY_PULLS)
    {
      tagDeviceApplyStandbyPulls(tagDeviceTable[i].device);
    }
  }
}
