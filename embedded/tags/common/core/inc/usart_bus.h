/**
 * @file usart_bus.h
 * @brief Synchronous-USART descriptors, lifecycle helpers, and byte transfers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_USART_BUS_H
#define TAG_CORE_USART_BUS_H

#include "core_sync.h"

#include "hal.h"

#include <stdbool.h>
#include <stdint.h>

#define USART2_ALTERNATE_FUNCTION 7


/** @name Synchronous-USART device model
 * Synchronous-USART bus helpers.
 *
 * Some tags use an STM32 USART in clocked synchronous mode as a small
 * SPI-like sensor bus. Core owns the raw byte-transfer mechanics because they
 * are MCU/peripheral plumbing. Sensor code should build device register
 * protocols on top of this layer through sensor_io.
 * @{
 */
typedef struct {
  uint32_t brr;
  uint32_t cr1;
  uint32_t cr2;
  uint32_t cr3;
} TagUsartSyncConfig;

/** Standby pull policy applied while preparing a USART-backed device for sleep. */
typedef enum {
  TAG_USART_SLEEP_FLOAT,
  TAG_USART_SLEEP_SAFE_IDLE,
  TAG_USART_SLEEP_CUSTOM
} TagUsartSleepPolicy;

/** Board-line description for one synchronous-USART device. */
typedef struct {
  USART_TypeDef *usart;
  binary_semaphore_t *mutex;
  int alternate_function;
  const TagUsartSyncConfig *config;
  ioline_t cs;
  ioline_t sck;
  ioline_t tx;
  ioline_t rx;
  ioline_t pwr;
  uint8_t dummy;
  TagUsartSleepPolicy sleep_policy;
} TagUsartDevice;

extern const TagUsartSyncConfig tagUsart2SyncDefaultConfig;

#define TAG_USART2_SYNC_DEVICE_DEFAULTS                                      \
  .usart = USART2, .mutex = &USART2mutex, .config = &tagUsart2SyncDefaultConfig, .alternate_function = USART2_ALTERNATE_FUNCTION
/** @} */

/** @name USART active-state tracking
 * Tracking hooks let Stop2 suspend only the USART peripherals that were active,
 * then restore them afterward without changing device power or chip select.
 * @{
 */
/**
 * @brief Report whether a USART peripheral is currently enabled by a bus session.
 *
 * @param[in] usart STM32 USART peripheral to inspect.
 * @return true when the peripheral is tracked as active.
 */
bool tagUsartIsOn(USART_TypeDef *usart);

/**
 * @brief Mark a USART peripheral active for Stop2 suspend tracking.
 *
 * @param[in] usart STM32 USART peripheral to mark active.
 */
void tagMarkUsartOn(USART_TypeDef *usart);

/**
 * @brief Mark a USART peripheral inactive and clear any suspended state.
 *
 * @param[in] usart STM32 USART peripheral to mark inactive.
 */
void tagMarkUsartOff(USART_TypeDef *usart);

/**
 * @brief Disable currently active USART peripherals before Stop2 entry.
 */
void tagUsartDisableActiveForStop(void);

/**
 * @brief Re-enable USART peripherals suspended by Stop2 entry.
 */
void tagUsartEnableActiveAfterStop(void);
/** @} */

/** @name USART descriptor access
 * Accessors keep raw register users tied to a descriptor rather than separate
 * global peripheral assumptions.
 * @{
 */
/**
 * @brief Return the STM32 peripheral referenced by a USART device descriptor.
 *
 * @param[in] device USART device descriptor.
 * @return STM32 USART peripheral for the device.
 */
static inline USART_TypeDef *tagUsartDevicePeripheral(
    const TagUsartDevice *device)
{
  return device->usart;
}
/** @} */

/** @name USART device power
 * Device power controls the optional switched power line and safe idle pin
 * states for the device. It does not start or stop the MCU USART peripheral.
 * @{
 */
/**
 * @brief Assert device power and put chip select into a safe idle state.
 *
 * @param[in] device USART device descriptor to power.
 */
void tagUsartDevicePowerOn(const TagUsartDevice *device);

/**
 * @brief Remove device power and return USART pins to analog low-leakage mode.
 *
 * @param[in] device USART device descriptor to power down.
 */
void tagUsartDevicePowerOff(const TagUsartDevice *device);
/** @} */

/** @name USART bus sessions
 * Bus sessions enable or disable the MCU USART peripheral using the device's
 * synchronous configuration. Callers normally power the device first, then
 * begin the bus; shutdown happens in the reverse order.
 * @{
 */
/**
 * @brief Claim the shared bus and enable the USART peripheral for this device.
 *
 * @param[in] device USART device descriptor whose session should begin.
 */
void tagUsartBusBegin(const TagUsartDevice *device);

/**
 * @brief Disable the USART peripheral and release the shared bus.
 *
 * @param[in] device USART device descriptor whose session should end.
 */
void tagUsartBusEnd(const TagUsartDevice *device);
/** @} */

/** @name USART low-power preparation
 * Apply the device's standby pull policy before entering low-power stop or
 * standby states. This is separate from normal bus-session teardown.
 * @{
 */
/**
 * @brief Apply standby pull policy for a USART-backed device.
 *
 * @param[in] device USART device descriptor whose sleep policy should be applied.
 */
void tagUsartDevicePrepareSleep(const TagUsartDevice *device);
/** @} */

/** @name USART raw transfers
 * Byte-transfer helpers used by register adapters once a bus session and
 * chip-select phase have been established.
 * @{
 */
/**
 * @brief Write bytes and discard the full-duplex response stream.
 *
 * @param[in] device USART device descriptor.
 * @param[in] buf Bytes to transmit.
 * @param[in] len Number of bytes to transmit.
 */
void tagUsartWrite(const TagUsartDevice *device, const uint8_t *buf,
                   uint32_t len);

/**
 * @brief Read bytes by clocking the descriptor's dummy byte.
 *
 * @param[in] device USART device descriptor.
 * @param[out] buf Buffer that receives bytes from the device.
 * @param[in] len Number of bytes to read.
 */
void tagUsartRead(const TagUsartDevice *device, uint8_t *buf, uint32_t len);

/**
 * @brief Assert the USART device chip-select line.
 *
 * @param[in] device USART device descriptor to select.
 */
void tagUsartSelect(const TagUsartDevice *device);

/**
 * @brief Deassert the USART device chip-select line.
 *
 * @param[in] device USART device descriptor to deselect.
 */
void tagUsartDeselect(const TagUsartDevice *device);

/**
 * Convenience transaction wrappers. These assert chip select, perform one raw
 * byte transfer, then release chip select. Use the lower-level select/write/read
 * calls directly when a device protocol needs multiple phases under one CS.
 * @{
 */
/**
 * @brief Write one complete USART transaction under chip select.
 *
 * @param[in] device USART device descriptor.
 * @param[in] buf Bytes to transmit.
 * @param[in] len Number of bytes to transmit.
 */
static inline void tagUsartBusWrite(const TagUsartDevice *device,
                                    const uint8_t *buf, uint32_t len)
{
  tagUsartSelect(device);
  tagUsartWrite(device, buf, len);
  tagUsartDeselect(device);
}

/**
 * @brief Read one complete USART transaction under chip select.
 *
 * @param[in] device USART device descriptor.
 * @param[out] buf Buffer that receives bytes from the device.
 * @param[in] len Number of bytes to read.
 */
static inline void tagUsartBusRead(const TagUsartDevice *device, uint8_t *buf,
                                   uint32_t len)
{
  tagUsartSelect(device);
  tagUsartRead(device, buf, len);
  tagUsartDeselect(device);
}
/** @} */
/** @} */

#endif
