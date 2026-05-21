#ifndef TAG_CORE_DEVICE_H
#define TAG_CORE_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Tag/family device standby hooks.
 *
 * Core pwr.c owns the universal MCU standby sequence and the universal RTC
 * handling. Tag/family devices.c files own the non-universal peripherals:
 * external flash, pressure sensors, magnetometers, and tag-specific
 * accelerometer wiring.
 *
 * These hooks keep pwr.c flat and readable while leaving the actual device
 * behavior beside each tag/family's hardware descriptors.
 *
 * The wakeup-source hooks are for non-universal wake inputs, such as
 * accelerometer interrupt pins. The configure hook returns false if the input
 * changed while entering standby and the caller should abort the sleep attempt.
 */
void tagDevicesPrepareStandby(uint32_t state);
void tagDevicesApplyStandbyPins(void);
void tagDevicesDisableWakeupSources(void);
bool tagDevicesConfigureWakeupSources(uint32_t state, bool is_active);
void tagDevicesDeinit(void);

#endif
