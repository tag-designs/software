#ifndef TAG_SENSOR_IO_H
#define TAG_SENSOR_IO_H

#include "bus_device.h"
#include "hal.h"

#include <stdint.h>

/*
 * Shared sensor register-bus helpers.
 *
 * Device drivers should keep sensor-specific command formats in the driver
 * when they are unusual. Core bus modules own shared peripheral mechanics;
 * this file covers sensor-oriented register adapters including the common
 * ST-style SPI/USART register convention used by several pressure/IMU parts:
 *
 *   write command: register address
 *   read command:  register address OR read_mask
 *
 * Sensor drivers describe register access with a `TagRegisterDevice`. The
 * register device carries the register protocol kind, the bus descriptor used
 * by that protocol, and the ST-style command masks when the device uses that
 * convention.
 */
typedef int (*TagRegisterWrite)(const void *io, uint8_t reg,
                                const uint8_t *buf, uint32_t len);
typedef int (*TagRegisterRead)(const void *io, uint8_t reg, uint8_t *buf,
                               uint32_t len);

/*
 * Register-device descriptor.
 *
 * Most register devices use one of the built-in protocol/bus combinations.
 * Custom keeps the old callback escape hatch for unusual register protocols
 * without forcing every normal device through a function-pointer bus wrapper.
 */
typedef enum {
  TAG_REGISTER_ST,
  TAG_REGISTER_I2C,
  TAG_REGISTER_CUSTOM
} TagRegisterKind;

typedef struct {
  TagRegisterKind kind;
  TagBusDevice bus;
  struct {
    TagRegisterRead read_register;
    TagRegisterWrite write_register;
    const void *context;
  } custom;
  uint8_t read_mask;
  uint8_t write_mask;
} TagRegisterDevice;

// Generic dispatch helpers used by descriptor-backed sensor drivers.
int tagRegisterWrite(const TagRegisterDevice *device, uint8_t reg,
                     const uint8_t *buf, uint32_t len);
int tagRegisterRead(const TagRegisterDevice *device, uint8_t reg, uint8_t *buf,
                    uint32_t len);

// Simple I2C register transactions. The adapter context is a TagI2cDevice.
int tagI2cWriteRegister(const void *io, uint8_t reg, const uint8_t *buf,
                        uint32_t len);
int tagI2cReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                       uint32_t len);

// ST-style register transactions over SPI with one CS assertion per register.
int tagStSpiWriteRegisterDevice(const TagRegisterDevice *registers,
                                uint8_t reg, const uint8_t *buf,
                                uint32_t len);
int tagStSpiReadRegisterDevice(const TagRegisterDevice *registers,
                               uint8_t reg, uint8_t *buf, uint32_t len);

// ST-style register transactions over synchronous USART used as SPI-lite.
int tagStUsartWriteRegisterDevice(const TagRegisterDevice *registers,
                                  uint8_t reg, const uint8_t *buf,
                                  uint32_t len);
int tagStUsartReadRegisterDevice(const TagRegisterDevice *registers,
                                 uint8_t reg, uint8_t *buf, uint32_t len);

#endif
