/**
 * @file storage_spi.h
 * @brief Conservative SPI transaction framing helpers for external flash.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_STORAGE_SPI_H
#define TAG_STORAGE_SPI_H

#include "spi_bus.h"

#include <stdint.h>

#ifndef TAG_STORAGE_SPI_POLL_LIMIT
#define TAG_STORAGE_SPI_POLL_LIMIT 100000U
#endif

static inline bool tagStorageSpiWait(SPI_TypeDef *spi, uint32_t mask)
{
  uint32_t timeout = TAG_STORAGE_SPI_POLL_LIMIT;

  while ((spi->SR & mask) == 0U)
  {
    if ((spi->SR & SPI_SR_OVR) != 0U)
    {
      volatile uint32_t dummy = spi->DR;
      dummy = spi->SR;
      (void)dummy;
      return false;
    }
    if (timeout-- == 0U)
      return false;
  }
  return true;
}

/** @name Storage SPI transaction helpers
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
 * @{
 */

/**
 * @brief Write bytes to flash using byte-at-a-time full-duplex pacing.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[in] buf Bytes to transmit.
 * @param[in] n Number of bytes to transmit.
 */
static inline bool tagStorageSpiWrite(const TagSpiDevice *device,
                                      const uint8_t *buf, uint32_t n)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;

  while (n--)
  {
    *spidr = *buf++;
    if (!tagStorageSpiWait(spi, SPI_SR_RXNE))
      return false;
    (void)*spidr;
  }
  return true;
}

/**
 * @brief Read bytes from flash while keeping the SPI clock continuously fed.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[out] buf Destination buffer.
 * @param[in] n Number of bytes to read.
 */
static inline bool tagStorageSpiRead(const TagSpiDevice *device, uint8_t *buf,
                                     uint32_t n)
{
    SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
    volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;

    if (n == 0) return true;

    /* --- Prime the pipeline: send first dummy byte before entering loop --- */
    *spidr = device->dummy;

    while (n-- > 1)
    {
        /*
         * Send the NEXT dummy byte as soon as TXE is set — this keeps the
         * SPI bus continuously clocked while we wait for the current RX byte.
         * TXE rises the moment the shift register has accepted the byte from
         * DR, long before RXNE rises for that same byte, so there is no gap.
         */
        if (!tagStorageSpiWait(spi, SPI_SR_TXE))
            return false;
        *spidr = device->dummy;

        /* Now collect the byte that was clocked in for the previous write. */
        if (!tagStorageSpiWait(spi, SPI_SR_RXNE))
            return false;
        *buf++ = *spidr;
    }

    /* --- Drain the last byte that is still in flight --- */
    if (!tagStorageSpiWait(spi, SPI_SR_RXNE))
        return false;
    *buf = *spidr;
    return true;
}

/**
 * @brief Send a 24-bit flash address in command byte order.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[in] address 24-bit flash address.
 */
static inline bool tagStorageSpiAddress(const TagSpiDevice *device,
                                        uint32_t address)
{
  uint8_t buf[3];

  buf[0] = address >> 16;
  buf[1] = address >> 8;
  buf[2] = address & 0xff;
  return tagStorageSpiWrite(device, buf, sizeof(buf));
}

/**
 * @brief Send one standalone flash command under chip select.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[in] cmd Flash command byte.
 */
static inline bool tagStorageSpiCommand(const TagSpiDevice *device, uint8_t cmd)
{
  bool ok;

  tagSpiSelect(device);
  ok = tagStorageSpiWrite(device, &cmd, sizeof(cmd));
  tagSpiDeselect(device);
  return ok;
}

/**
 * @brief Send a flash command and 24-bit address under chip select.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[in] cmd Flash command byte.
 * @param[in] address 24-bit flash address.
 */
static inline bool tagStorageSpiCommandAddress(const TagSpiDevice *device,
                                               uint8_t cmd,
                                               uint32_t address)
{
  bool ok;

  tagSpiSelect(device);
  ok = tagStorageSpiWrite(device, &cmd, sizeof(cmd)) &&
       tagStorageSpiAddress(device, address);
  tagSpiDeselect(device);
  return ok;
}

/**
 * @brief Send a flash command and receive response bytes under chip select.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[in] cmd Flash command byte.
 * @param[out] buf Destination buffer.
 * @param[in] num Number of response bytes to read.
 */
static inline bool tagStorageSpiCommandReceive(const TagSpiDevice *device,
                                               uint8_t cmd, uint8_t *buf,
                                               uint32_t num)
{
  bool ok;

  tagSpiSelect(device);
  ok = tagStorageSpiWrite(device, &cmd, sizeof(cmd)) &&
       tagStorageSpiRead(device, buf, num);
  tagSpiDeselect(device);
  return ok;
}

/**
 * @brief Send a command/address pair and receive response bytes.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[in] cmd Flash command byte.
 * @param[in] address 24-bit flash address.
 * @param[out] buf Destination buffer.
 * @param[in] num Number of response bytes to read.
 */
static inline bool tagStorageSpiCommandAddressReceive(
    const TagSpiDevice *device,
                                                      uint8_t cmd,
                                                      uint32_t address,
                                                      uint8_t *buf,
                                                      uint32_t num)
{
  bool ok;

  tagSpiSelect(device);
  ok = tagStorageSpiWrite(device, &cmd, sizeof(cmd)) &&
       tagStorageSpiAddress(device, address) &&
       tagStorageSpiRead(device, buf, num);
  tagSpiDeselect(device);
  return ok;
}

/**
 * @brief Send a command/address pair followed by payload bytes.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[in] cmd Flash command byte.
 * @param[in] address 24-bit flash address.
 * @param[in] buf Payload bytes to send.
 * @param[in] num Number of payload bytes to send.
 */
static inline bool tagStorageSpiCommandAddressSend(const TagSpiDevice *device,
                                                   uint8_t cmd,
                                                   uint32_t address,
                                                   const uint8_t *buf,
                                                   uint32_t num)
{
  bool ok;

  tagSpiSelect(device);
  ok = tagStorageSpiWrite(device, &cmd, sizeof(cmd)) &&
       tagStorageSpiAddress(device, address) &&
       tagStorageSpiWrite(device, buf, num);
  tagSpiDeselect(device);
  return ok;
}
/** @} */

#endif
