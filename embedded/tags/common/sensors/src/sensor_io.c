#include "sensor_io.h"

#define TAG_I2C_REGISTER_MAX_WRITE 16

int tagRegisterWrite(const TagRegisterBus *bus, uint8_t reg,
                     const uint8_t *buf, uint32_t len)
{
  return bus->write_register(bus->context, reg, buf, len);
}

int tagRegisterRead(const TagRegisterBus *bus, uint8_t reg, uint8_t *buf,
                    uint32_t len)
{
  return bus->read_register(bus->context, reg, buf, len);
}

static inline I2CDriver *tagI2cDeviceDriver(const TagI2cDevice *device)
{
  return device->controller->driver;
}

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

  return i2cMasterTransmitTimeout(tagI2cDeviceDriver(device), device->address,
                                  txbuf, len + 1, 0, 0, device->timeout);
}

int tagI2cReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                       uint32_t len)
{
  const TagI2cDevice *device = (const TagI2cDevice *)io;

  return i2cMasterTransmitTimeout(tagI2cDeviceDriver(device), device->address,
                                  &reg, 1, buf, len, device->timeout);
}

int tagStSpiWriteRegister(const void *io, uint8_t reg, const uint8_t *buf,
                          uint32_t len)
{
  const TagStSpiRegisterBus *spi = (const TagStSpiRegisterBus *)io;
  uint8_t command = (uint8_t)((reg & (uint8_t)~spi->read_mask) |
                             spi->write_mask);

  tagSpiSelect(spi->device);
  /*
   * Keep the register command and payload in one CS-framed transaction. Some
   * SPI-like sensors latch the command on CS rising, so callers must not split
   * a register write into separately selected command and data transfers.
   */
  tagSpiWrite(spi->device, &command, 1);
  tagSpiWrite(spi->device, buf, len);
  tagSpiDeselect(spi->device);

  return 0;
}

int tagStSpiReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                         uint32_t len)
{
  const TagStSpiRegisterBus *spi = (const TagStSpiRegisterBus *)io;
  uint8_t command = (uint8_t)(reg | spi->read_mask);

  tagSpiSelect(spi->device);
  tagSpiWrite(spi->device, &command, 1);
  tagSpiRead(spi->device, buf, len);
  tagSpiDeselect(spi->device);

  return 0;
}

int tagStUsartWriteRegister(const void *io, uint8_t reg, const uint8_t *buf,
                            uint32_t len)
{
  const TagStUsartRegisterBus *usart = (const TagStUsartRegisterBus *)io;
  uint8_t command = (uint8_t)((reg & (uint8_t)~usart->read_mask) |
                             usart->write_mask);

  tagUsartSelect(usart->device);
  /*
   * Synchronous-USART devices use CS for the same transaction framing as SPI;
   * the command byte and payload must stay under the same assertion.
   */
  tagUsartWrite(usart->device, &command, 1);
  tagUsartWrite(usart->device, buf, len);
  tagUsartDeselect(usart->device);

  return 0;
}

int tagStUsartReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                           uint32_t len)
{
  const TagStUsartRegisterBus *usart = (const TagStUsartRegisterBus *)io;
  uint8_t command = (uint8_t)(reg | usart->read_mask);

  tagUsartSelect(usart->device);
  tagUsartWrite(usart->device, &command, 1);
  tagUsartRead(usart->device, buf, len);
  tagUsartDeselect(usart->device);

  return 0;
}
