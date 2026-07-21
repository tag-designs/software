/**
 * @file sensor_io.h
 * @brief Register-device descriptors and bus adapters for sensor drivers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_SENSOR_IO_H
#define TAG_SENSOR_IO_H

#include "bus_device.h"
#include "hal.h"

#include <stdint.h>

/** @name Register-device model
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
 * @{
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
/** @} */

/** @name Register dispatch helpers
 * Generic dispatch helpers used by descriptor-backed sensor drivers.
 * @{
 */
/**
 * @brief Write bytes to a sensor register using the descriptor's protocol.
 *
 * @param[in] device Register-device descriptor.
 * @param[in] reg Register address.
 * @param[in] buf Bytes to write.
 * @param[in] len Number of bytes to write.
 * @return 0/MSG_OK on success or a protocol-specific error.
 */
int tagRegisterWrite(const TagRegisterDevice *device, uint8_t reg,
                     const uint8_t *buf, uint32_t len);

/**
 * @brief Read bytes from a sensor register using the descriptor's protocol.
 *
 * @param[in] device Register-device descriptor.
 * @param[in] reg Register address.
 * @param[out] buf Destination buffer.
 * @param[in] len Number of bytes to read.
 * @return 0/MSG_OK on success or a protocol-specific error.
 */
int tagRegisterRead(const TagRegisterDevice *device, uint8_t reg, uint8_t *buf,
                    uint32_t len);
/** @} */

/** @name I2C register adapter
 * Simple I2C register transactions. The adapter context is a TagI2cDevice.
 * @{
 */
/**
 * @brief Write bytes to an I2C register device.
 *
 * @param[in] io TagI2cDevice context.
 * @param[in] reg Register address.
 * @param[in] buf Bytes to write.
 * @param[in] len Number of bytes to write.
 * @return MSG_OK on success or a ChibiOS I2C error.
 */
int tagI2cWriteRegister(const void *io, uint8_t reg, const uint8_t *buf,
                        uint32_t len);

/**
 * @brief Read bytes from an I2C register device.
 *
 * @param[in] io TagI2cDevice context.
 * @param[in] reg Register address.
 * @param[out] buf Destination buffer.
 * @param[in] len Number of bytes to read.
 * @return MSG_OK on success or a ChibiOS I2C error.
 */
int tagI2cReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                       uint32_t len);
/** @} */

/** @name ST-style SPI register adapter
 * ST-style register transactions over SPI with one CS assertion per register.
 * @{
 */
/**
 * @brief Write bytes to an ST-style register device over SPI.
 *
 * @param[in] registers Register-device descriptor containing SPI bus details.
 * @param[in] reg Register address before the descriptor's write mask is applied.
 * @param[in] buf Bytes to write after the command byte.
 * @param[in] len Number of bytes to write.
 * @return MSG_OK on success or a register-transport error.
 */
int tagStSpiWriteRegisterDevice(const TagRegisterDevice *registers,
                                uint8_t reg, const uint8_t *buf,
                                uint32_t len);
/**
 * @brief Read bytes from an ST-style register device over SPI.
 *
 * @param[in] registers Register-device descriptor containing SPI bus details.
 * @param[in] reg Register address before the descriptor's read mask is applied.
 * @param[out] buf Destination buffer.
 * @param[in] len Number of bytes to read.
 * @return MSG_OK on success or a register-transport error.
 */
int tagStSpiReadRegisterDevice(const TagRegisterDevice *registers,
                               uint8_t reg, uint8_t *buf, uint32_t len);

/**
 * @brief Read bytes from an ST-style SPI register using DMA.
 *
 * This helper does not retry on failure, which keeps FIFO-style registers from
 * being consumed twice after a partial transfer.
 *
 * @param[in] registers Register-device descriptor containing SPI bus details.
 * @param[in] reg Register address before the descriptor's read mask is applied.
 * @param[out] buf Destination buffer.
 * @param[in] len Number of bytes to read.
 * @return MSG_OK on success or a register-transport error.
 */
int tagStSpiReadRegisterDeviceDma(const TagRegisterDevice *registers,
                                  uint8_t reg, uint8_t *buf, uint32_t len);
/** @} */

/** @name ST-style synchronous-USART register adapter
 * ST-style register transactions over synchronous USART used as SPI-lite.
 * @{
 */
/**
 * @brief Write bytes to an ST-style register device over synchronous USART.
 *
 * @param[in] registers Register-device descriptor containing USART bus details.
 * @param[in] reg Register address before the descriptor's write mask is applied.
 * @param[in] buf Bytes to write after the command byte.
 * @param[in] len Number of bytes to write.
 * @return MSG_OK on success or a register-transport error.
 */
int tagStUsartWriteRegisterDevice(const TagRegisterDevice *registers,
                                  uint8_t reg, const uint8_t *buf,
                                  uint32_t len);
/**
 * @brief Read bytes from an ST-style register device over synchronous USART.
 *
 * @param[in] registers Register-device descriptor containing USART bus details.
 * @param[in] reg Register address before the descriptor's read mask is applied.
 * @param[out] buf Destination buffer.
 * @param[in] len Number of bytes to read.
 * @return MSG_OK on success or a register-transport error.
 */
int tagStUsartReadRegisterDevice(const TagRegisterDevice *registers,
                                 uint8_t reg, uint8_t *buf, uint32_t len);
/** @} */

#endif
