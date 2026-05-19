#ifndef TAG_CORE_I2C_BUS_H
#define TAG_CORE_I2C_BUS_H

#include "hal.h"

#include <stdint.h>

// Board-line and fallback-I2C configuration for one I2C device.
typedef struct {
  I2CDriver *driver;
  binary_semaphore_t *mutex;
  I2CConfig config;
  ioline_t sda;
  ioline_t scl;
  ioline_t pwr;
} TagI2cDevice;

/*
 * Generic I2C register helpers for devices that use the common
 * "write register address, then transfer payload" pattern.
 *
 * Power sequencing, pin state, mutex ownership, and I2C controller start/stop
 * remain in the tag power layer. This layer only performs transactions on an
 * already-enabled I2C controller.
 */
typedef struct {
  I2CDriver *driver;
  uint8_t address;
  uint32_t timeout;
} TagI2cRegisterBus;

void tagI2cDeviceOn(const TagI2cDevice *device);
void tagI2cDeviceOff(const TagI2cDevice *device);
void tagI2cDevicePrepareSleep(const TagI2cDevice *device);

int tagI2cWriteRegister(const void *io, uint8_t reg, const uint8_t *buf,
                        uint32_t len);
int tagI2cReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                       uint32_t len);

#endif
