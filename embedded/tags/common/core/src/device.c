#include "device.h"

/*
 * Weak defaults for optional tag/family device standby hooks.
 *
 * Tags with non-universal peripherals override these from their devices.c file.
 * Keeping the defaults here lets simple or legacy tags continue to use core
 * pwr.c without providing a device binding file.
 */
void __attribute__((weak)) tagDevicesPrepareStandby(uint32_t state)
{
  (void)state;
}

void __attribute__((weak)) tagDevicesApplyStandbyPins(void)
{
}

void __attribute__((weak)) tagDevicesDeinit(void)
{
}
