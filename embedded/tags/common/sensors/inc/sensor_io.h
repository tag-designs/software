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
 * when they are unusual. Core `i2c_bus` and `spi_bus` own shared bus mechanics;
 * this file covers sensor-oriented register adapters including the common
 * ST-style SPI register convention used by several pressure/IMU parts:
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

typedef struct {
  TagRegisterRead read_register;
  TagRegisterWrite write_register;
  const void *context;
  const TagBusDevice *bus;
  uint8_t read_mask;
  uint8_t write_mask;
} TagRegisterDevice;

int tagRegisterWrite(const TagRegisterDevice *device, uint8_t reg,
                     const uint8_t *buf, uint32_t len);
int tagRegisterRead(const TagRegisterDevice *device, uint8_t reg, uint8_t *buf,
                    uint32_t len);
void tagRegisterBusBegin(const TagRegisterDevice *device);
void tagRegisterBusEnd(const TagRegisterDevice *device);

int tagI2cWriteRegister(const void *io, uint8_t reg, const uint8_t *buf,
                        uint32_t len);
int tagI2cReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                       uint32_t len);

int tagStSpiWriteRegisterDevice(const void *io, uint8_t reg,
                                const uint8_t *buf, uint32_t len);
int tagStSpiReadRegisterDevice(const void *io, uint8_t reg, uint8_t *buf,
                               uint32_t len);

int tagStUsartWriteRegisterDevice(const void *io, uint8_t reg,
                                  const uint8_t *buf, uint32_t len);
int tagStUsartReadRegisterDevice(const void *io, uint8_t reg, uint8_t *buf,
                                 uint32_t len);

#endif
