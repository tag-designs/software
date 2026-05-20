#ifndef TAG_STORAGE_SPI_H
#define TAG_STORAGE_SPI_H

#include "spi_bus.h"

#include <stdint.h>

/*
 * SPI transaction helpers for external flash drivers.
 *
 * Flash chips keep chip select asserted across command, optional address, and
 * data phases. These helpers centralize that framing while leaving chip
 * commands and polling rules in the individual storage drivers.
 *
 * These helpers deliberately use conservative byte-at-a-time transfers rather
 * than the pipelined stream helpers in spi_bus.c. The flash erase/write command
 * path has historically depended on reading each full-duplex byte before
 * sending the next byte; keeping that pacing here preserves the known-good
 * storage behavior while the generic SPI layer remains available for devices
 * that tolerate streaming transactions.
 */

static inline void tagStorageSpiWrite(const TagSpiBus *bus, const uint8_t *buf,
                                      uint32_t n)
{
  SPI_TypeDef *spi = tagSpiBusPeripheral(bus);
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;

  while (n--)
  {
    *spidr = *buf++;
    while ((spi->SR & SPI_SR_RXNE) == 0)
    {
      ;
    }
    (void)*spidr;
  }
}

static inline void tagStorageSpiRead(const TagSpiBus *bus, uint8_t *buf,
                                     uint32_t n)
{
  SPI_TypeDef *spi = tagSpiBusPeripheral(bus);
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;

  while (n--)
  {
    *spidr = bus->dummy;
    while ((spi->SR & SPI_SR_RXNE) == 0)
    {
      ;
    }
    *buf++ = *spidr;
  }
}

static inline void tagStorageSpiAddress(const TagSpiBus *bus, uint32_t address)
{
  uint8_t buf[3];

  buf[0] = address >> 16;
  buf[1] = address >> 8;
  buf[2] = address & 0xff;
  tagStorageSpiWrite(bus, buf, sizeof(buf));
}

static inline void tagStorageSpiCommand(const TagSpiBus *bus, uint8_t cmd)
{
  tagSpiSelect(bus);
  tagStorageSpiWrite(bus, &cmd, sizeof(cmd));
  tagSpiDeselect(bus);
}

static inline void tagStorageSpiCommandAddress(const TagSpiBus *bus,
                                               uint8_t cmd,
                                               uint32_t address)
{
  tagSpiSelect(bus);
  tagStorageSpiWrite(bus, &cmd, sizeof(cmd));
  tagStorageSpiAddress(bus, address);
  tagSpiDeselect(bus);
}

static inline void tagStorageSpiCommandReceive(const TagSpiBus *bus,
                                               uint8_t cmd, uint8_t *buf,
                                               uint32_t num)
{
  tagSpiSelect(bus);
  tagStorageSpiWrite(bus, &cmd, sizeof(cmd));
  tagStorageSpiRead(bus, buf, num);
  tagSpiDeselect(bus);
}

static inline void tagStorageSpiCommandAddressReceive(const TagSpiBus *bus,
                                                      uint8_t cmd,
                                                      uint32_t address,
                                                      uint8_t *buf,
                                                      uint32_t num)
{
  tagSpiSelect(bus);
  tagStorageSpiWrite(bus, &cmd, sizeof(cmd));
  tagStorageSpiAddress(bus, address);
  tagStorageSpiRead(bus, buf, num);
  tagSpiDeselect(bus);
}

static inline void tagStorageSpiCommandAddressSend(const TagSpiBus *bus,
                                                   uint8_t cmd,
                                                   uint32_t address,
                                                   const uint8_t *buf,
                                                   uint32_t num)
{
  tagSpiSelect(bus);
  tagStorageSpiWrite(bus, &cmd, sizeof(cmd));
  tagStorageSpiAddress(bus, address);
  tagStorageSpiWrite(bus, buf, num);
  tagSpiDeselect(bus);
}

#endif
