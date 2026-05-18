#include "sensor_io.h"

#define TAG_I2C_REGISTER_MAX_WRITE 16

/*
 * ChibiOS I2C register helpers for devices that use the conventional
 * "write register address, then transfer payload" pattern.
 */
int tagI2cWriteRegister(const void *io, uint8_t reg, const uint8_t *buf,
                        uint32_t len)
{
  const TagI2cRegisterIO *i2c = (const TagI2cRegisterIO *)io;
  uint8_t txbuf[TAG_I2C_REGISTER_MAX_WRITE + 1];

  if (len > TAG_I2C_REGISTER_MAX_WRITE) {
    return MSG_RESET;
  }

  txbuf[0] = reg;
  for (uint32_t i = 0; i < len; i++) {
    txbuf[i + 1] = buf[i];
  }

  return i2cMasterTransmitTimeout(i2c->driver, i2c->address, txbuf, len + 1,
                                  0, 0, i2c->timeout);
}

int tagI2cReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                       uint32_t len)
{
  const TagI2cRegisterIO *i2c = (const TagI2cRegisterIO *)io;

  return i2cMasterTransmitTimeout(i2c->driver, i2c->address, &reg, 1, buf,
                                  len, i2c->timeout);
}

/*
 * Polling SPI1 byte transfers shared by simple sensor drivers.
 *
 * Existing sensor code drives SPI1 directly rather than through ChibiOS SPI
 * transactions, so these helpers preserve that behavior while centralizing the
 * repeated full-duplex drain/read loops. Bus power, SPI configuration, and
 * sleep-state pin policy remain in the tag power layer.
 */
void tagSpiWrite(const uint8_t *buf, uint32_t len)
{
  volatile uint8_t *spidr = (volatile uint8_t *)&SPI1->DR;

  while (len--) {
    *spidr = *buf++;
    while ((SPI1->SR & SPI_SR_RXNE) == 0)
      ;
    (void)*spidr;
  }
}

void tagSpiRead(uint8_t *buf, uint32_t len)
{
  volatile uint8_t *spidr = (volatile uint8_t *)&SPI1->DR;

  while (len--) {
    *spidr = 0xff;
    while ((SPI1->SR & SPI_SR_RXNE) == 0)
      ;
    *buf++ = *spidr;
  }
}

int tagStSpiWriteRegister(const void *io, uint8_t reg, const uint8_t *buf,
                          uint32_t len)
{
  const TagStSpiRegisterIO *spi = (const TagStSpiRegisterIO *)io;
  uint8_t command = (uint8_t)((reg & (uint8_t)~spi->read_mask) |
                             spi->write_mask);

  palClearLine(spi->cs);
  tagSpiWrite(&command, 1);
  tagSpiWrite(buf, len);
  palSetLine(spi->cs);

  return 0;
}

int tagStSpiReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                         uint32_t len)
{
  const TagStSpiRegisterIO *spi = (const TagStSpiRegisterIO *)io;
  uint8_t command = (uint8_t)(reg | spi->read_mask);

  palClearLine(spi->cs);
  tagSpiWrite(&command, 1);
  tagSpiRead(buf, len);
  palSetLine(spi->cs);

  return 0;
}

/*
 * USART synchronous-mode byte transfers for tags that use USART2 as an
 * SPI-like sensor bus. The power layer owns clocking and register setup; these
 * helpers only move bytes through the already-configured peripheral.
 */
void tagUsartWrite(USART_TypeDef *usart, const uint8_t *buf, uint32_t len)
{
  volatile uint8_t *tdr = (volatile uint8_t *)&usart->TDR;
  volatile uint8_t *rdr = (volatile uint8_t *)&usart->RDR;

  while (len--) {
    *tdr = *buf++;
    while ((usart->ISR & USART_ISR_RXNE) == 0)
      ;
    (void)*rdr;
  }
}

void tagUsartRead(USART_TypeDef *usart, uint8_t dummy, uint8_t *buf,
                  uint32_t len)
{
  volatile uint8_t *tdr = (volatile uint8_t *)&usart->TDR;
  volatile uint8_t *rdr = (volatile uint8_t *)&usart->RDR;

  while (len--) {
    *tdr = dummy;
    while ((usart->ISR & USART_ISR_RXNE) == 0)
      ;
    *buf++ = *rdr;
  }
}

int tagStUsartWriteRegister(const void *io, uint8_t reg, const uint8_t *buf,
                            uint32_t len)
{
  const TagStUsartRegisterIO *usart = (const TagStUsartRegisterIO *)io;
  uint8_t command = (uint8_t)((reg & (uint8_t)~usart->read_mask) |
                             usart->write_mask);

  palClearLine(usart->cs);
  tagUsartWrite(usart->usart, &command, 1);
  tagUsartWrite(usart->usart, buf, len);
  palSetLine(usart->cs);

  return 0;
}

int tagStUsartReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                           uint32_t len)
{
  const TagStUsartRegisterIO *usart = (const TagStUsartRegisterIO *)io;
  uint8_t command = (uint8_t)(reg | usart->read_mask);

  palClearLine(usart->cs);
  tagUsartWrite(usart->usart, &command, 1);
  tagUsartRead(usart->usart, usart->dummy, buf, len);
  palSetLine(usart->cs);

  return 0;
}
