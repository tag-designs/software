#include "spi_bus.h"

/*
 * Polling SPI byte transfers shared by simple peripheral drivers.
 *
 * Existing tag code drives SPI controller registers directly rather than
 * through ChibiOS SPI transactions, so these helpers preserve that behavior
 * while centralizing the repeated full-duplex drain/read loops. Bus power, SPI
 * configuration, and sleep-state pin policy remain in the tag power layer.
 */
void tagSpiWrite(SPI_TypeDef *spi, const uint8_t *buf, uint32_t len)
{
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;
  uint32_t read_len = len;

  while (len || read_len) {
    while (len && (spi->SR & SPI_SR_TXE)) {
      *spidr = *buf++;
      len--;
    }
    while (read_len && (spi->SR & SPI_SR_RXNE)) {
      (void)*spidr;
      read_len--;
    }
  }
}

void tagSpiRead(SPI_TypeDef *spi, uint8_t *buf, uint32_t len)
{
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;
  uint32_t read_len = len;

  while (len || read_len) {
    while (len && (spi->SR & SPI_SR_TXE)) {
      *spidr = 0xff;
      len--;
    }
    while (read_len && (spi->SR & SPI_SR_RXNE)) {
      *buf++ = *spidr;
      read_len--;
    }
  }
}

void tagSpiSelect(const TagSpiBus *bus)
{
  palClearLine(bus->cs);
}

void tagSpiDeselect(const TagSpiBus *bus)
{
  palSetLine(bus->cs);
}

void tagSpiBusWrite(const TagSpiBus *bus, const uint8_t *buf, uint32_t len)
{
  tagSpiSelect(bus);
  tagSpiWrite(bus->spi, buf, len);
  tagSpiDeselect(bus);
}

void tagSpiBusRead(const TagSpiBus *bus, uint8_t *buf, uint32_t len)
{
  tagSpiSelect(bus);
  tagSpiRead(bus->spi, buf, len);
  tagSpiDeselect(bus);
}
