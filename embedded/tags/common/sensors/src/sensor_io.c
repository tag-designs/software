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
  tagSpiWrite(spi->bus->spi, &command, 1);
  tagSpiWrite(spi->bus->spi, buf, len);
  tagSpiDeselect(spi->bus);

  return 0;
}

int tagStSpiReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                         uint32_t len)
{
  const TagStSpiRegisterBus *spi = (const TagStSpiRegisterBus *)io;
  uint8_t command = (uint8_t)(reg | spi->read_mask);

  tagSpiSelect(spi->bus);
  tagSpiWrite(spi->bus->spi, &command, 1);
  tagSpiRead(spi->bus->spi, buf, len);
  tagSpiDeselect(spi->bus);

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
  const TagStUsartRegisterBus *usart = (const TagStUsartRegisterBus *)io;
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
  const TagStUsartRegisterBus *usart = (const TagStUsartRegisterBus *)io;
  uint8_t command = (uint8_t)(reg | usart->read_mask);

  palClearLine(usart->cs);
  tagUsartWrite(usart->usart, &command, 1);
  tagUsartRead(usart->usart, usart->dummy, buf, len);
  palSetLine(usart->cs);

  return 0;
}
