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
/* Public API contract documented in sensor_io.h. */
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

/* Public API contract documented in sensor_io.h. */
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
/* Public API contract documented in sensor_io.h. */
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

/* Public API contract documented in sensor_io.h. */
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
 * @brief Transfer an SPI register write payload using the best available path.
 *
 * @param[in] device SPI device descriptor whose CS is already asserted.
 * @param[in] buf Payload bytes to transmit after the command byte.
 * @param[in] len Number of payload bytes to transmit.
 * @return true when the full payload was transmitted.
 */
static inline bool tagStSpiWritePayload(const TagSpiDevice *device,
                                        const uint8_t *buf,
                                        uint32_t len)
{
  if (len <= TAG_SPI_POLLED_TRANSFER_MAX)
    return tagSpiPolledSend(device, buf, len);
  return tagSpiWrite(device, buf, len);
}

/**
 * @brief Transfer an SPI register read payload using the best available path.
 *
 * @param[in] device SPI device descriptor whose CS is already asserted.
 * @param[out] buf Buffer that receives bytes after the command byte.
 * @param[in] len Number of payload bytes to read.
 * @return true when the full payload was received.
 */
static inline bool tagStSpiReadPayload(const TagSpiDevice *device,
                                       uint8_t *buf,
                                       uint32_t len)
{
  if (len <= TAG_SPI_POLLED_TRANSFER_MAX)
    return tagSpiPolledReceive(device, buf, len);
  return tagSpiRead(device, buf, len);
}

/**
 * @brief Send one ST-style SPI register command plus payload.
 *
 * @details Uses a single small polled transaction when the command and payload
 *          fit the polled-transfer buffer; otherwise it sends the command
 *          first and then transfers the payload while CS remains asserted.
 *
 * @param[in] device SPI device descriptor whose CS is already asserted.
 * @param[in] command Register command byte including protocol masks.
 * @param[in] buf Payload bytes to write.
 * @param[in] len Number of payload bytes to write.
 * @return true when both command and payload were transmitted.
 */
static inline bool tagStSpiWriteRegisterPayload(const TagSpiDevice *device,
                                                uint8_t command,
                                                const uint8_t *buf,
                                                uint32_t len)
{
  uint8_t txbuf[TAG_SPI_POLLED_TRANSFER_MAX];

  if (len < TAG_SPI_POLLED_TRANSFER_MAX) {
    txbuf[0] = command;
    for (uint32_t i = 0; i < len; i++)
      txbuf[i + 1U] = buf[i];
    return tagSpiPolledSend(device, txbuf, len + 1U);
  }

  return tagSpiPolledSend(device, &command, sizeof(command)) &&
         tagStSpiWritePayload(device, buf, len);
}

/* Public API contract documented in sensor_io.h. */
int tagStSpiWriteRegisterDevice(const TagRegisterDevice *registers,
                                uint8_t reg, const uint8_t *buf,
                                uint32_t len)
{
  const TagSpiDevice *device = tagBusSpiDevice(&registers->bus);
  uint8_t command = (uint8_t)((reg & (uint8_t)~registers->read_mask) |
                             registers->write_mask);
  bool ok;

  tagSpiSelect(device);
  ok = tagStSpiWriteRegisterPayload(device, command, buf, len);
  tagSpiDeselect(device);

  return ok ? MSG_OK : MSG_RESET;
}

/* Public API contract documented in sensor_io.h. */
int tagStSpiReadRegisterDevice(const TagRegisterDevice *registers,
                               uint8_t reg, uint8_t *buf, uint32_t len)
{
  const TagSpiDevice *device = tagBusSpiDevice(&registers->bus);
  uint8_t command = (uint8_t)(reg | registers->read_mask);
  bool ok;

  tagSpiSelect(device);
  ok = tagSpiPolledSend(device, &command, sizeof(command)) &&
       tagStSpiReadPayload(device, buf, len);
  tagSpiDeselect(device);

  return ok ? MSG_OK : MSG_RESET;
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
/* Public API contract documented in sensor_io.h. */
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

/* Public API contract documented in sensor_io.h. */
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
