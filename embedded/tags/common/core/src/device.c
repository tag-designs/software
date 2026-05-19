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

void tagDeviceApplyStandbyPins(const TagDevice *device)
{
  if (device && device->apply_standby_pins)
  {
    device->apply_standby_pins(device->context);
  }
}

void tagDeviceTablePrepareStandby(uint32_t state)
{
  /*
   * Protocol-level standby preparation.
   *
   * Examples: tell external flash to enter deep sleep, but only for system
   * states where the tag will remain asleep long enough to benefit.
   */
  for (size_t i = 0; i < tagDeviceCount; i++)
  {
    if (tagDeviceTable[i].flags & TAG_DEVICE_PREPARE_STANDBY)
    {
      tagDevicePrepareStandby(tagDeviceTable[i].device, state);
    }
  }
}

void tagDeviceTableApplyStandbyPins(void)
{
  /*
   * MCU standby pin preparation.
   *
   * These callbacks should only program standby pullup/pulldown state for the
   * device's pins. They are separate from prepare_standby because some devices
   * need protocol commands before standby, while every board still needs safe
   * GPIO state before entering the processor low-power mode.
   */
  for (size_t i = 0; i < tagDeviceCount; i++)
  {
    if (tagDeviceTable[i].flags & TAG_DEVICE_APPLY_STANDBY_PINS)
    {
      tagDeviceApplyStandbyPins(tagDeviceTable[i].device);
    }
  }
}
