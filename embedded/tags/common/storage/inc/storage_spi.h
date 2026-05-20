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

static inline void tagStorageSpiWrite(const TagSpiDevice *device,
                                      const uint8_t *buf, uint32_t n)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
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

static inline void tagStorageSpiRead(const TagSpiDevice *device, uint8_t *buf,
                                     uint32_t n)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;

  while (n--)
  {
    *spidr = device->dummy;
    while ((spi->SR & SPI_SR_RXNE) == 0)
    {
      ;
    }
    *buf++ = *spidr;
  }
}

static inline void tagStorageSpiAddress(const TagSpiDevice *device,
                                        uint32_t address)
{
  uint8_t buf[3];

  buf[0] = address >> 16;
  buf[1] = address >> 8;
  buf[2] = address & 0xff;
  tagStorageSpiWrite(device, buf, sizeof(buf));
}

static inline void tagStorageSpiCommand(const TagSpiDevice *device, uint8_t cmd)
{
  tagSpiSelect(device);
  tagStorageSpiWrite(device, &cmd, sizeof(cmd));
  tagSpiDeselect(device);
}

static inline void tagStorageSpiCommandAddress(const TagSpiDevice *device,
                                               uint8_t cmd,
                                               uint32_t address)
{
  tagSpiSelect(device);
  tagStorageSpiWrite(device, &cmd, sizeof(cmd));
  tagStorageSpiAddress(device, address);
  tagSpiDeselect(device);
}

static inline void tagStorageSpiCommandReceive(const TagSpiDevice *device,
                                               uint8_t cmd, uint8_t *buf,
                                               uint32_t num)
{
  tagSpiSelect(device);
  tagStorageSpiWrite(device, &cmd, sizeof(cmd));
  tagStorageSpiRead(device, buf, num);
  tagSpiDeselect(device);
}

static inline void tagStorageSpiCommandAddressReceive(
    const TagSpiDevice *device,
                                                      uint8_t cmd,
                                                      uint32_t address,
                                                      uint8_t *buf,
                                                      uint32_t num)
{
  tagSpiSelect(device);
  tagStorageSpiWrite(device, &cmd, sizeof(cmd));
  tagStorageSpiAddress(device, address);
  tagStorageSpiRead(device, buf, num);
  tagSpiDeselect(device);
}

static inline void tagStorageSpiCommandAddressSend(const TagSpiDevice *device,
                                                   uint8_t cmd,
                                                   uint32_t address,
                                                   const uint8_t *buf,
                                                   uint32_t num)
{
  tagSpiSelect(device);
  tagStorageSpiWrite(device, &cmd, sizeof(cmd));
  tagStorageSpiAddress(device, address);
  tagStorageSpiWrite(device, buf, num);
  tagSpiDeselect(device);
}

#endif
