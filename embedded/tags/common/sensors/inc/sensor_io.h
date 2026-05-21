#ifndef TAG_SENSOR_IO_H
#define TAG_SENSOR_IO_H

#include "hal.h"
#include "i2c_bus.h"
#include "spi_bus.h"
#include "usart_bus.h"

#include <stdint.h>

/*
 * Shared sensor register-bus helpers.
 *
 * Device drivers should keep sensor-specific command formats in the driver
 * when they are unusual. Core bus modules own shared controller mechanics;
 * this file covers sensor-oriented register adapters including the common
 * ST-style SPI/USART register convention used by several pressure/IMU parts:
 *
 *   write command: register address
 *   read command:  register address OR read_mask
 *
 * Sensor drivers describe register access with a `TagRegisterDevice`. The
 * register device combines a small read/write vtable, the concrete bus-session
 * descriptor, and the ST-style command masks when the device uses that
 * convention.
 */
typedef int (*TagRegisterWrite)(const void *io, uint8_t reg,
                                const uint8_t *buf, uint32_t len);
typedef int (*TagRegisterRead)(const void *io, uint8_t reg, uint8_t *buf,
                               uint32_t len);

/*
 * Register-device descriptor.
 *
 * `read_register` and `write_register` implement the sensor's register
 * protocol. `bus` tells the driver how to power and open the concrete SPI,
 * USART, or I2C device before register transactions. `context` is optional:
 * leave it NULL for adapters that need the full descriptor, or set it to a
 * smaller bus object when the adapter does not need masks or bus ops.
 */
typedef struct {
  TagRegisterRead read_register;
  TagRegisterWrite write_register;
  const void *context;
  const TagBusDevice *bus;
  uint8_t read_mask;
  uint8_t write_mask;
} TagRegisterDevice;

// Generic dispatch helpers used by descriptor-backed sensor drivers.
int tagRegisterWrite(const TagRegisterDevice *device, uint8_t reg,
                     const uint8_t *buf, uint32_t len);
int tagRegisterRead(const TagRegisterDevice *device, uint8_t reg, uint8_t *buf,
                    uint32_t len);
void tagRegisterBusBegin(const TagRegisterDevice *device);
void tagRegisterBusEnd(const TagRegisterDevice *device);

// Simple I2C register transactions. The adapter context is a TagI2cDevice.
int tagI2cWriteRegister(const void *io, uint8_t reg, const uint8_t *buf,
                        uint32_t len);
int tagI2cReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                       uint32_t len);

// ST-style register transactions over SPI with one CS assertion per register.
int tagStSpiWriteRegisterDevice(const void *io, uint8_t reg,
                                const uint8_t *buf, uint32_t len);
int tagStSpiReadRegisterDevice(const void *io, uint8_t reg, uint8_t *buf,
                               uint32_t len);

// ST-style register transactions over synchronous USART used as SPI-lite.
int tagStUsartWriteRegisterDevice(const void *io, uint8_t reg,
                                  const uint8_t *buf, uint32_t len);
int tagStUsartReadRegisterDevice(const void *io, uint8_t reg, uint8_t *buf,
                                 uint32_t len);

#endif
