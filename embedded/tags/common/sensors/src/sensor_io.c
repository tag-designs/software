/**
 * @file sensor_io.c
 * @brief Register-device dispatch and I2C/SPI/USART sensor register adapters.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "sensor_io.h"

#define TAG_I2C_REGISTER_MAX_WRITE 16

/** @name Register-device dispatch
 * Register-device dispatch.
 *
 * Descriptor-backed drivers see one TagRegisterDevice shape. The descriptor
 * keeps a concrete bus descriptor by value, so dispatch is an explicit switch
 * over the register protocol and bus kind instead of a type-erased vtable.
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
                     const uint8_t *buf, uint32_t len)
{
  switch (device->kind) {
  case TAG_REGISTER_ST:
    switch (device->bus.kind) {
    case TAG_BUS_SPI:
      return tagStSpiWriteRegisterDevice(device, reg, buf, len);
    case TAG_BUS_USART:
      return tagStUsartWriteRegisterDevice(device, reg, buf, len);
    case TAG_BUS_I2C:
      return MSG_RESET;
    }
    return MSG_RESET;

  case TAG_REGISTER_I2C:
    return tagI2cWriteRegister(tagBusI2cDevice(&device->bus), reg, buf, len);

  case TAG_REGISTER_CUSTOM:
    return device->custom.write_register(device->custom.context, reg, buf, len);
  }

  return MSG_RESET;
}

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
                    uint32_t len)
{
  switch (device->kind) {
  case TAG_REGISTER_ST:
    switch (device->bus.kind) {
    case TAG_BUS_SPI:
      return tagStSpiReadRegisterDevice(device, reg, buf, len);
    case TAG_BUS_USART:
      return tagStUsartReadRegisterDevice(device, reg, buf, len);
    case TAG_BUS_I2C:
      return MSG_RESET;
    }
    return MSG_RESET;

  case TAG_REGISTER_I2C:
    return tagI2cReadRegister(tagBusI2cDevice(&device->bus), reg, buf, len);

  case TAG_REGISTER_CUSTOM:
    return device->custom.read_register(device->custom.context, reg, buf, len);
  }

  return MSG_RESET;
}
/** @} */

/** @name I2C register adapter
 * I2C register adapter.
 *
 * I2C register access does not need ST-style read/write command masks. The
 * adapter context is the TagI2cDevice because address and timeout belong to
 * the concrete device descriptor.
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
                        uint32_t len)
{
  const TagI2cDevice *device = (const TagI2cDevice *)io;
  uint8_t txbuf[TAG_I2C_REGISTER_MAX_WRITE + 1];

  if (len > TAG_I2C_REGISTER_MAX_WRITE) {
    return MSG_RESET;
  }

  txbuf[0] = reg;
  for (uint32_t i = 0; i < len; i++) {
    txbuf[i + 1] = buf[i];
  }

  return tagI2cMasterTransmitTimeout(device, device->address, txbuf, len + 1,
                                     0, 0, device->timeout);
}

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
                       uint32_t len)
{
  const TagI2cDevice *device = (const TagI2cDevice *)io;

  return tagI2cMasterTransmitTimeout(device, device->address, &reg, 1, buf,
                                     len, device->timeout);
}
/** @} */

/** @name ST-style SPI register adapter
 * ST-style SPI register adapter.
 *
 * This keeps command and payload under one CS assertion. Several SPI sensors
 * latch the register command on CS rising, so callers must not split a register
 * write into separately selected command and data transfers.
 * @{
 */
/**
 * @brief Write bytes to an ST-style SPI register device.
 *
 * @param[in] registers Register-device descriptor.
 * @param[in] reg Register address.
 * @param[in] buf Bytes to write.
 * @param[in] len Number of bytes to write.
 * @return 0 on success.
 */
int tagStSpiWriteRegisterDevice(const TagRegisterDevice *registers,
                                uint8_t reg, const uint8_t *buf,
                                uint32_t len)
{
  const TagSpiDevice *device = tagBusSpiDevice(&registers->bus);
  uint8_t command = (uint8_t)((reg & (uint8_t)~registers->read_mask) |
                             registers->write_mask);

  tagSpiSelect(device);
  tagSpiWrite(device, &command, 1);
  tagSpiWrite(device, buf, len);
  tagSpiDeselect(device);

  return 0;
}

/**
 * @brief Read bytes from an ST-style SPI register device.
 *
 * @param[in] registers Register-device descriptor.
 * @param[in] reg Register address.
 * @param[out] buf Destination buffer.
 * @param[in] len Number of bytes to read.
 * @return 0 on success.
 */
int tagStSpiReadRegisterDevice(const TagRegisterDevice *registers,
                               uint8_t reg, uint8_t *buf, uint32_t len)
{
  const TagSpiDevice *device = tagBusSpiDevice(&registers->bus);
  uint8_t command = (uint8_t)(reg | registers->read_mask);

  tagSpiSelect(device);
  tagSpiWrite(device, &command, 1);
  tagSpiRead(device, buf, len);
  tagSpiDeselect(device);

  return 0;
}
/** @} */

/** @name ST-style synchronous-USART register adapter
 * ST-style synchronous-USART register adapter.
 *
 * Some tags use USART in synchronous mode as a small SPI-like bus. The
 * register framing mirrors the SPI adapter: assert CS, send command, transfer
 * payload, release CS.
 * @{
 */
/**
 * @brief Write bytes to an ST-style synchronous-USART register device.
 *
 * @param[in] registers Register-device descriptor.
 * @param[in] reg Register address.
 * @param[in] buf Bytes to write.
 * @param[in] len Number of bytes to write.
 * @return 0 on success.
 */
int tagStUsartWriteRegisterDevice(const TagRegisterDevice *registers,
                                  uint8_t reg, const uint8_t *buf,
                                  uint32_t len)
{
  const TagUsartDevice *device = tagBusUsartDevice(&registers->bus);
  uint8_t command = (uint8_t)((reg & (uint8_t)~registers->read_mask) |
                             registers->write_mask);

  tagUsartSelect(device);
  tagUsartWrite(device, &command, 1);
  tagUsartWrite(device, buf, len);
  tagUsartDeselect(device);

  return 0;
}

/**
 * @brief Read bytes from an ST-style synchronous-USART register device.
 *
 * @param[in] registers Register-device descriptor.
 * @param[in] reg Register address.
 * @param[out] buf Destination buffer.
 * @param[in] len Number of bytes to read.
 * @return 0 on success.
 */
int tagStUsartReadRegisterDevice(const TagRegisterDevice *registers,
                                 uint8_t reg, uint8_t *buf, uint32_t len)
{
  const TagUsartDevice *device = tagBusUsartDevice(&registers->bus);
  uint8_t command = (uint8_t)(reg | registers->read_mask);

  tagUsartSelect(device);
  tagUsartWrite(device, &command, 1);
  tagUsartRead(device, buf, len);
  tagUsartDeselect(device);

  return 0;
}
/** @} */
