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

#include <stdbool.h>
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

/**
 * @def TAG_I2C_BUS_CLEAR
 * @brief Recover a stuck I2C bus around sessions, startup and standby.
 *
 * @details Off by default so existing targets keep their exact images. The
 *          recovery only acts when SDA actually reads low, so enabling it
 *          changes behaviour on a broken bus and nowhere else; the reason for
 *          the switch is code size on the smaller parts, not risk.
 *
 * @see tagI2cBusClearIfStuck()
 */
#ifndef TAG_I2C_BUS_CLEAR
#define TAG_I2C_BUS_CLEAR 0
#endif

/**
 * @def TAG_I2C_DEFAULT_ALTERNATE_FUNCTION
 * @brief Default STM32 alternate-function number used by most STM32 I2C pins.
 */
#ifndef TAG_I2C_DEFAULT_ALTERNATE_FUNCTION
#define TAG_I2C_DEFAULT_ALTERNATE_FUNCTION 4U
#endif

/**
 * @enum TagI2cBackendKind
 * @brief I2C backend selected by a board-level controller descriptor.
 */
typedef enum {
  TAG_I2C_BACKEND_HARDWARE, ///< Use a ChibiOS hardware I2CDriver.
  TAG_I2C_BACKEND_SOFTWARE  ///< Use the project software-I2C backend.
} TagI2cBackendKind;

/**
 * @brief Shared I2C controller register setup and bus arbitration.
 */
typedef struct {
  TagI2cBackendKind backend; ///< Hardware or software backend selection.
  binary_semaphore_t *mutex; ///< Shared bus lock for transaction sessions.
  union {
    I2CDriver *hardware;         ///< Hardware driver when backend is hardware.
    TagSoftI2cDriver *software;  ///< Software driver when backend is software.
  } driver;
#if TAG_I2C_BUS_CLEAR
  /**
   * @brief Optional peripheral reset for bus recovery, or NULL.
   *
   * @details Board code supplies the RCC reset for this controller instance,
   *          for example @c rccResetI2C1. Bus recovery needs it because
   *          clearing the wire is not sufficient on its own: the peripheral
   *          latches its own BUSY state from the bus, and a controller that
   *          saw the bus go quiet mid-transfer can stay stuck no matter what
   *          the pins subsequently read.
   *
   * @note Kept as a pointer rather than an instance test inside i2c_bus.c so
   *       the shared layer needs no knowledge of which I2Cx a board wired up.
   */
  void (*reset)(void);
#endif
} TagI2cController;

/**
 * @enum TagI2cSleepPolicy
 * @brief Standby pull policy applied while preparing an I2C-backed device.
 */
typedef enum {
  TAG_I2C_SLEEP_PULLUP, ///< Bias SCL/SDA with standby pull-ups.
  TAG_I2C_SLEEP_FLOAT,  ///< Leave bus pins floating/analog for sleep.
  TAG_I2C_SLEEP_CUSTOM  ///< Tag-specific code owns sleep pin state.
} TagI2cSleepPolicy;

/**
 * @brief Board-line description for one I2C device on a shared controller.
 */
typedef struct {
  const TagI2cController *controller; ///< Shared controller descriptor.
  union {
    const I2CConfig *hardware;        ///< ChibiOS config for hardware backend.
    const TagSoftI2cConfig *software; ///< Software-I2C config for software backend.
  } config;
  ioline_t sda;                       ///< SDA board line.
  ioline_t scl;                       ///< SCL board line.
  ioline_t pwr;                       ///< Optional switched-power line.
  uint8_t address;                    ///< Default 7-bit I2C device address.
  uint8_t alternate_function;         ///< STM32 alternate-function selector.
  uint32_t timeout;                   ///< Default transfer timeout.
  TagI2cSleepPolicy sleep_policy;     ///< Pin policy for low-power entry.
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

/**
 * @brief Recover a bus that a slave is holding, if it is holding one.
 *
 * @details A core reset in the middle of an I2C byte -- which a monitor attach
 *          causes routinely, since it connects under reset -- leaves the
 *          addressed slave driving SDA low and waiting for clocks that never
 *          arrive. The master cannot issue a START while the bus is not idle,
 *          so every device on the controller fails for the rest of that boot.
 *          Nine SCL pulses let the slave finish its byte and release SDA; a
 *          STOP then returns every device on the bus to idle.
 *
 *          Observed on an IMUTagNandBmp581, where the RV-3028 and the BMM350
 *          share one controller: SDA low with SCL high at the moment of
 *          failure, both devices unreachable together, and the condition
 *          persisting for the whole boot.
 *
 * @param[in] device Device whose controller and board lines should be cleared.
 * @return true when the bus was stuck and a recovery sequence was issued,
 *         false when the bus was already idle or recovery does not apply.
 *
 * @note Does nothing unless SDA actually reads low, so a healthy bus is never
 *       disturbed, and nothing when the device has a switched power line that
 *       is currently deasserted -- driving an unpowered part would inject
 *       current through its protection diodes.
 *
 * @warning Drives SCL and SDA directly. Callers must own the controller mutex,
 *          or run before the scheduler starts.
 *
 * @see tagI2cBusBegin(), tagI2cBusEnd(), tagSoftI2cBusClear()
 */
#if TAG_I2C_BUS_CLEAR
bool tagI2cBusClearIfStuck(const TagI2cDevice *device);
#endif
/** @} */

#endif
