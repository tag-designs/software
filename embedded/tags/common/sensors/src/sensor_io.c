#include "sensor_io.h"

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

int tagStSpiWriteRegister(const void *io, uint8_t reg, const uint8_t *buf,
                          uint32_t len)
{
  const TagStSpiRegisterBus *spi = (const TagStSpiRegisterBus *)io;
  uint8_t command = (uint8_t)((reg & (uint8_t)~spi->read_mask) |
                             spi->write_mask);

  tagSpiSelect(spi->bus);
  /*
   * Keep the register command and payload in one CS-framed transaction. Some
   * SPI-like sensors latch the command on CS rising, so callers must not split
   * a register write into separately selected command and data transfers.
   */
  tagSpiWrite(tagSpiBusPeripheral(spi->bus), &command, 1);
  tagSpiWrite(tagSpiBusPeripheral(spi->bus), buf, len);
  tagSpiDeselect(spi->bus);

  return 0;
}

int tagStSpiReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                         uint32_t len)
{
  const TagStSpiRegisterBus *spi = (const TagStSpiRegisterBus *)io;
  uint8_t command = (uint8_t)(reg | spi->read_mask);

  tagSpiSelect(spi->bus);
  tagSpiWrite(tagSpiBusPeripheral(spi->bus), &command, 1);
  tagSpiRead(tagSpiBusPeripheral(spi->bus), buf, len);
  tagSpiDeselect(spi->bus);

  return 0;
}

int tagStUsartWriteRegister(const void *io, uint8_t reg, const uint8_t *buf,
                            uint32_t len)
{
  const TagStUsartRegisterBus *usart = (const TagStUsartRegisterBus *)io;
  uint8_t command = (uint8_t)((reg & (uint8_t)~usart->read_mask) |
                             usart->write_mask);

  tagUsartSelect(usart->bus);
  /*
   * Synchronous-USART devices use CS for the same transaction framing as SPI;
   * the command byte and payload must stay under the same assertion.
   */
  tagUsartWrite(tagUsartBusPeripheral(usart->bus), &command, 1);
  tagUsartWrite(tagUsartBusPeripheral(usart->bus), buf, len);
  tagUsartDeselect(usart->bus);

  return 0;
}

int tagStUsartReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                           uint32_t len)
{
  const TagStUsartRegisterBus *usart = (const TagStUsartRegisterBus *)io;
  uint8_t command = (uint8_t)(reg | usart->read_mask);

  tagUsartSelect(usart->bus);
  tagUsartWrite(tagUsartBusPeripheral(usart->bus), &command, 1);
  tagUsartRead(tagUsartBusPeripheral(usart->bus), usart->bus->dummy, buf,
               len);
  tagUsartDeselect(usart->bus);

  return 0;
}
