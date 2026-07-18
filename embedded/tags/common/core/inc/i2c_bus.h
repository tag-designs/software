/**
 * @file i2c_bus.h
 * @brief I2C controller and device lifecycle helpers for tag devices.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_I2C_BUS_H
#define TAG_CORE_I2C_BUS_H

#include "hal.h"
#include "tag_soft_i2c.h"

#include <stddef.h>
#include <stdint.h>

/** @name I2C device model
 * I2C bus helpers.
 *
 * Core owns controller setup, device power/session pin policy, and standby
 * pull policy for I2C-backed devices. Register reads and writes live in
 * sensor_io so SPI, USART, and I2C register devices share one adapter shape.
 * @{
 */

/** Default STM32 alternate-function number used by most STM32 I2C pins. */
#ifndef TAG_I2C_DEFAULT_ALTERNATE_FUNCTION
#define TAG_I2C_DEFAULT_ALTERNATE_FUNCTION 4U
#endif

/** I2C backend selected by a board-level controller descriptor. */
typedef enum {
  TAG_I2C_BACKEND_HARDWARE,
  TAG_I2C_BACKEND_SOFTWARE
} TagI2cBackendKind;

/** I2C controller register setup and bus arbitration. */
typedef struct {
  TagI2cBackendKind backend;
  binary_semaphore_t *mutex;
  union {
    I2CDriver *hardware;
    TagSoftI2cDriver *software;
  } driver;
} TagI2cController;

/** Standby pull policy applied while preparing an I2C-backed device for sleep. */
typedef enum {
  TAG_I2C_SLEEP_PULLUP,
  TAG_I2C_SLEEP_FLOAT,
  TAG_I2C_SLEEP_CUSTOM
} TagI2cSleepPolicy;

/** Board-line description for one I2C device attached to a shared controller. */
typedef struct {
  const TagI2cController *controller;
  union {
    const I2CConfig *hardware;
    const TagSoftI2cConfig *software;
  } config;
  ioline_t sda;
  ioline_t scl;
  ioline_t pwr;
  uint8_t address;
  uint8_t alternate_function;
  uint32_t timeout;
  TagI2cSleepPolicy sleep_policy;
} TagI2cDevice;

/** @} */

/** @name I2C controller lifecycle
 * Low-level controller hooks used by tagI2cBusBegin/End.
 *
 * I2C is a little different from the SPI/USART shims: each controller has a
 * selected backend, and enabling the controller needs the active device's
 * backend-specific config. Normal device code should call tagI2cBusBegin/End
 * instead of these routines unless it is deliberately managing the whole
 * shared I2C controller.
 * @{
 */
/**
 * @brief Initialize the ChibiOS I2C driver object for a shared controller.
 *
 * @param[in] controller Shared controller whose driver object should be
 *            initialized before first use.
 */
void tagI2cControllerObjectInit(const TagI2cController *controller);

/**
 * @brief Start an I2C controller with the active device configuration.
 *
 * @param[in] controller Shared controller to enable.
 * @param[in] device Active device supplying the backend-specific config.
 */
void tagI2cControllerEnable(const TagI2cController *controller,
                            const TagI2cDevice *device);

/**
 * @brief Stop an I2C controller after the active bus session ends.
 *
 * @param[in] controller Shared controller to disable.
 */
void tagI2cControllerDisable(const TagI2cController *controller);
/** @} */

/** @name I2C device power
 * Device power controls the optional switched power line and safe idle pin
 * states for the device. It does not start or stop the MCU I2C peripheral.
 * @{
 */
/**
 * @brief Assert a device's optional switched power line.
 *
 * @param[in] device I2C device descriptor whose power line should be enabled.
 */
void tagI2cDevicePowerOn(const TagI2cDevice *device);

/**
 * @brief Deassert a device's optional switched power line.
 *
 * @param[in] device I2C device descriptor whose power line should be disabled.
 */
void tagI2cDevicePowerOff(const TagI2cDevice *device);
/** @} */

/** @name I2C bus sessions
 * Bus sessions enable or disable the MCU I2C controller using the device's
 * configuration. Callers normally power the device first, then begin the bus;
 * shutdown happens in the reverse order.
 * @{
 */
/**
 * @brief Claim the shared controller and start a transaction session.
 *
 * @param[in] device I2C device descriptor supplying the controller and config.
 */
void tagI2cBusBegin(const TagI2cDevice *device);

/**
 * @brief Stop a transaction session and release the shared controller.
 *
 * @param[in] device I2C device descriptor whose controller should be released.
 */
void tagI2cBusEnd(const TagI2cDevice *device);
/** @} */

/** @name I2C transfers
 * Backend-neutral raw transfer helper used by register adapters.
 * @{
 */
/**
 * @brief Perform one I2C write/read transaction through a device's backend.
 *
 * @param[in] device I2C device descriptor.
 * @param[in] address 7-bit or 10-bit I2C address, matching the backend config.
 * @param[in] txbuf Transmit buffer.
 * @param[in] txbytes Number of bytes to transmit.
 * @param[out] rxbuf Optional receive buffer.
 * @param[in] rxbytes Number of bytes to receive.
 * @param[in] timeout ChibiOS timeout interval.
 * @return MSG_OK on success or a bus error.
 */
msg_t tagI2cMasterTransmitTimeout(const TagI2cDevice *device,
                                  i2caddr_t address,
                                  const uint8_t *txbuf, size_t txbytes,
                                  uint8_t *rxbuf, size_t rxbytes,
                                  sysinterval_t timeout);
/** @} */

/** @name I2C low-power preparation
 * Apply the device's standby pull policy before entering low-power stop or
 * standby states. This is separate from normal bus-session teardown.
 * @{
 */
/**
 * @brief Apply the standby pin policy for an I2C-backed device.
 *
 * @param[in] device I2C device descriptor whose sleep policy should be applied.
 */
void tagI2cDevicePrepareSleep(const TagI2cDevice *device);
/** @} */

#endif
