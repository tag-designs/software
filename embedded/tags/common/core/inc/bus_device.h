#ifndef TAG_CORE_BUS_DEVICE_H
#define TAG_CORE_BUS_DEVICE_H

#include "i2c_bus.h"
#include "spi_bus.h"
#include "usart_bus.h"

/*
 * Concrete bus descriptor.
 *
 * Devices are described at compile time, so this layer preserves the common
 * bus lifecycle API without type-erasing through void pointers or vtables.
 */
typedef enum {
  TAG_BUS_SPI,
  TAG_BUS_USART,
  TAG_BUS_I2C
} TagBusKind;

typedef struct {
  TagBusKind kind;
  union {
    TagSpiDevice spi;
    TagUsartDevice usart;
    TagI2cDevice i2c;
  } device;
} TagBusDevice;

#define TAG_BUS_SPI_INIT(...) \
  { .kind = TAG_BUS_SPI, .device.spi = { __VA_ARGS__ } }

#define TAG_BUS_USART_INIT(...) \
  { .kind = TAG_BUS_USART, .device.usart = { __VA_ARGS__ } }

#define TAG_BUS_I2C_INIT(...) \
  { .kind = TAG_BUS_I2C, .device.i2c = { __VA_ARGS__ } }

static inline const TagSpiDevice *tagBusSpiDevice(const TagBusDevice *bus)
{
  return &bus->device.spi;
}

static inline const TagUsartDevice *tagBusUsartDevice(const TagBusDevice *bus)
{
  return &bus->device.usart;
}

static inline const TagI2cDevice *tagBusI2cDevice(const TagBusDevice *bus)
{
  return &bus->device.i2c;
}

static inline void tagBusPowerOn(const TagBusDevice *bus)
{
  switch (bus->kind) {
  case TAG_BUS_SPI:
    tagSpiDevicePowerOn(tagBusSpiDevice(bus));
    break;
  case TAG_BUS_USART:
    tagUsartDevicePowerOn(tagBusUsartDevice(bus));
    break;
  case TAG_BUS_I2C:
    tagI2cDevicePowerOn(tagBusI2cDevice(bus));
    break;
  }
}

static inline void tagBusPowerOff(const TagBusDevice *bus)
{
  switch (bus->kind) {
  case TAG_BUS_SPI:
    tagSpiDevicePowerOff(tagBusSpiDevice(bus));
    break;
  case TAG_BUS_USART:
    tagUsartDevicePowerOff(tagBusUsartDevice(bus));
    break;
  case TAG_BUS_I2C:
    tagI2cDevicePowerOff(tagBusI2cDevice(bus));
    break;
  }
}

static inline void tagBusBegin(const TagBusDevice *bus)
{
  switch (bus->kind) {
  case TAG_BUS_SPI:
    tagSpiBusBegin(tagBusSpiDevice(bus));
    break;
  case TAG_BUS_USART:
    tagUsartBusBegin(tagBusUsartDevice(bus));
    break;
  case TAG_BUS_I2C:
    tagI2cBusBegin(tagBusI2cDevice(bus));
    break;
  }
}

static inline void tagBusEnd(const TagBusDevice *bus)
{
  switch (bus->kind) {
  case TAG_BUS_SPI:
    tagSpiBusEnd(tagBusSpiDevice(bus));
    break;
  case TAG_BUS_USART:
    tagUsartBusEnd(tagBusUsartDevice(bus));
    break;
  case TAG_BUS_I2C:
    tagI2cBusEnd(tagBusI2cDevice(bus));
    break;
  }
}

static inline void tagBusPrepareSleep(const TagBusDevice *bus)
{
  switch (bus->kind) {
  case TAG_BUS_SPI:
    tagSpiDevicePrepareSleep(tagBusSpiDevice(bus));
    break;
  case TAG_BUS_USART:
    tagUsartDevicePrepareSleep(tagBusUsartDevice(bus));
    break;
  case TAG_BUS_I2C:
    tagI2cDevicePrepareSleep(tagBusI2cDevice(bus));
    break;
  }
}

#endif
