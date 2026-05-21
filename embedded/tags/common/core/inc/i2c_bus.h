#ifndef TAG_CORE_I2C_BUS_H
#define TAG_CORE_I2C_BUS_H

#include "bus_device.h"

#include "hal.h"

#include <stdint.h>

// I2C controller register setup and bus arbitration.
typedef struct {
  I2CDriver *driver;
  binary_semaphore_t *mutex;
} TagI2cController;

// Standby pull policy applied while preparing an I2C-backed device for sleep.
typedef enum {
  TAG_I2C_SLEEP_PULLUP,
  TAG_I2C_SLEEP_FLOAT,
  TAG_I2C_SLEEP_CUSTOM
} TagI2cSleepPolicy;

// Board-line description for one I2C device attached to a shared controller.
typedef struct {
  const TagI2cController *controller;
  const I2CConfig *config;
  ioline_t sda;
  ioline_t scl;
  ioline_t pwr;
  uint8_t address;
  uint32_t timeout;
  TagI2cSleepPolicy sleep_policy;
} TagI2cDevice;

extern const TagI2cController tagI2c1DefaultController;
extern const TagBusOps tagI2cBusOps;

/*
 * Low-level controller hooks used by tagI2cBusBegin/End.
 *
 * I2C is a little different from the SPI/USART shims: ChibiOS keeps the bus
 * timing and software-I2C line behavior in an I2CConfig, so enabling the
 * controller always needs both the shared controller and the active device's
 * config. Normal device code should call tagI2cBusBegin/End instead of these
 * routines unless it is deliberately managing the whole shared I2C controller.
 */
void tagI2cControllerEnable(const TagI2cController *controller,
                            const I2CConfig *config);
void tagI2cControllerDisable(const TagI2cController *controller);

/*
 * Device power controls the optional switched power line and safe idle pin
 * states for the device. It does not start or stop the MCU I2C peripheral.
 */
void tagI2cDevicePowerOn(const TagI2cDevice *device);
void tagI2cDevicePowerOff(const TagI2cDevice *device);

/*
 * Bus sessions enable or disable the MCU I2C controller using the device's
 * configuration. Callers normally power the device first, then begin the bus;
 * shutdown happens in the reverse order.
 */
void tagI2cBusBegin(const TagI2cDevice *device);
void tagI2cBusEnd(const TagI2cDevice *device);

/*
 * Apply the device's standby pull policy before entering low-power stop or
 * standby states. This is separate from normal bus-session teardown.
 */
void tagI2cDevicePrepareSleep(const TagI2cDevice *device);

#endif
