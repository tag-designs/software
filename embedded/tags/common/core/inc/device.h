/**
 * @file device.h
 * @brief Tag/family device hooks used by the common low-power sequence.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_DEVICE_H
#define TAG_CORE_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

/** @name Device lifecycle hooks
 * Tag/family device lifecycle hooks.
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
 * @{
 */
typedef enum {
  TAG_DEVICE_POWER_BOOT_CLEANUP,
  TAG_DEVICE_POWER_RUNTIME_DEINIT,
  TAG_DEVICE_POWER_STANDBY_ENTRY,
  TAG_DEVICE_POWER_TERMINAL_ENTRY
} TagDevicePowerReason;

/**
 * @brief Initialize tag-owned device runtime state.
 *
 * Called after ChibiOS startup, before device descriptors are used.
 */
void tagDevicesInit(void);

/**
 * @brief Apply tag-owned device power policy for a common lifecycle phase.
 *
 * @param[in] reason Common lifecycle phase that is quiescing the devices.
 * @param[in] state Current state-machine state.
 */
void tagDevicesApplyPowerState(TagDevicePowerReason reason, uint32_t state);

/**
 * @brief Give tag-specific devices a chance to enter their standby state.
 *
 * @param[in] state Application state that is about to enter standby.
 */
void tagDevicesPrepareStandby(uint32_t state);

/**
 * @brief Apply GPIO modes needed after tag devices have been quiesced.
 */
void tagDevicesApplyStandbyPins(void);

/**
 * @brief Disable non-universal device wake sources before reconfiguration.
 */
void tagDevicesDisableWakeupSources(void);

/**
 * @brief Configure tag-specific wake sources for the requested standby state.
 *
 * @param[in] state Application state that is about to sleep.
 * @param[in] is_active Whether the sleep attempt begins from an active state.
 * @return true when standby may proceed, or false when the caller should abort.
 */
bool tagDevicesConfigureWakeupSources(uint32_t state, bool is_active);

/**
 * @brief Deinitialize tag-specific devices during a final shutdown path.
 */
void tagDevicesDeinit(void);
/** @} */

#endif
