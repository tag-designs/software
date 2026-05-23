/**
 * @file bus_device.h
 * @brief Tagged bus-device union and common bus lifecycle dispatch helpers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_BUS_DEVICE_H
#define TAG_CORE_BUS_DEVICE_H

#include "i2c_bus.h"
#include "spi_bus.h"
#include "usart_bus.h"

/** @name Concrete bus descriptors
 * Concrete bus descriptor.
 *
 * Devices are described at compile time, so this layer preserves the common
 * bus lifecycle API without type-erasing through void pointers or vtables.
 * @{
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

/** Initialize a TagBusDevice that carries a TagSpiDevice descriptor. */
#define TAG_BUS_SPI_INIT(...) \
  { .kind = TAG_BUS_SPI, .device.spi = { __VA_ARGS__ } }

/** Initialize a TagBusDevice that carries a TagUsartDevice descriptor. */
#define TAG_BUS_USART_INIT(...) \
  { .kind = TAG_BUS_USART, .device.usart = { __VA_ARGS__ } }

/** Initialize a TagBusDevice that carries a TagI2cDevice descriptor. */
#define TAG_BUS_I2C_INIT(...) \
  { .kind = TAG_BUS_I2C, .device.i2c = { __VA_ARGS__ } }
/** @} */

/** @name Typed bus accessors
 * Accessors keep union member selection localized and make call sites read like
 * they are still using concrete bus-device descriptors.
 * @{
 */
/**
 * @brief Return the SPI descriptor stored in a tagged bus device.
 *
 * @param[in] bus Tagged bus device whose kind is TAG_BUS_SPI.
 * @return Pointer to the embedded SPI descriptor.
 */
static inline const TagSpiDevice *tagBusSpiDevice(const TagBusDevice *bus)
{
  return &bus->device.spi;
}

/**
 * @brief Return the USART descriptor stored in a tagged bus device.
 *
 * @param[in] bus Tagged bus device whose kind is TAG_BUS_USART.
 * @return Pointer to the embedded USART descriptor.
 */
static inline const TagUsartDevice *tagBusUsartDevice(const TagBusDevice *bus)
{
  return &bus->device.usart;
}

/**
 * @brief Return the I2C descriptor stored in a tagged bus device.
 *
 * @param[in] bus Tagged bus device whose kind is TAG_BUS_I2C.
 * @return Pointer to the embedded I2C descriptor.
 */
static inline const TagI2cDevice *tagBusI2cDevice(const TagBusDevice *bus)
{
  return &bus->device.i2c;
}
/** @} */

/** @name Common bus lifecycle dispatch
 * Inline dispatch preserves a compact storage/sensor interface while allowing
 * each concrete bus layer to own its power, session, and sleep behavior.
 * @{
 */
/**
 * @brief Power on the physical device represented by a tagged bus descriptor.
 *
 * @param[in] bus Tagged bus device to power.
 */
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

/**
 * @brief Power off the physical device represented by a tagged bus descriptor.
 *
 * @param[in] bus Tagged bus device to power down.
 */
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

/**
 * @brief Begin an exclusive transaction session for a tagged bus device.
 *
 * @param[in] bus Tagged bus device whose bus session should begin.
 */
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

/**
 * @brief End an exclusive transaction session for a tagged bus device.
 *
 * @param[in] bus Tagged bus device whose bus session should end.
 */
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

/**
 * @brief Apply the bus-specific sleep policy for a tagged bus device.
 *
 * @param[in] bus Tagged bus device to prepare for low-power entry.
 */
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
/** @} */

#endif
