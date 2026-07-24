/**
 * @file storage_spi.h
 * @brief Conservative SPI transaction framing helpers for external flash.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_STORAGE_SPI_H
#define TAG_STORAGE_SPI_H

#include "core_types.h"
#include "spi_bus.h"

#include <stdint.h>

/** @name Storage SPI transaction helpers
 * SPI transaction helpers for external flash drivers.
 *
 * Flash chips keep chip select asserted across command, optional address, and
 * data phases. These helpers centralize that framing while leaving chip
 * commands and polling rules in the individual storage drivers.
 *
 * Command and address phases use polled transfers to avoid DMA setup overhead.
 * Larger data phases use the generic stream helpers in spi_bus.c.
 * @{
 */

/**
 * @brief Write bytes to flash, polling command-sized transfers.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[in] buf Bytes to transmit.
 * @param[in] n Number of bytes to transmit.
 */
static inline bool tagStorageSpiWrite(const TagSpiDevice *device,
                                      const uint8_t *buf, uint32_t n)
{
  if (n <= TAG_SPI_POLLED_TRANSFER_MAX)
    return tagSpiPolledSend(device, buf, n);
  return tagSpiWrite(device, buf, n);
}

/**
 * @brief Read bytes from flash, polling command-sized transfers.
 *
 * @param[in] device SPI device descriptor for the flash.
 * @param[out] buf Destination buffer.
 * @param[in] n Number of bytes to read.
 */
static inline bool tagStorageSpiRead(const TagSpiDevice *device, uint8_t *buf,
                                     uint32_t n)
{
  if (n <= TAG_SPI_POLLED_TRANSFER_MAX)
    return tagSpiPolledReceive(device, buf, n);
  return tagSpiRead(device, buf, n);
}

/**
 * @brief Bulk write bytes to flash after command/address framing.
 *
 * Command-sized payloads stay on the polled path; larger data phases use the
 * stream helper so DMA-backed targets avoid byte-at-a-time payload transfers.
 */
static inline bool tagStorageSpiBlockWrite(const TagSpiDevice *device,
                                           const uint8_t *buf, uint32_t n)
{
  if (n <= TAG_SPI_POLLED_TRANSFER_MAX)
    return tagSpiPolledSend(device, buf, n);
  return tagSpiWrite(device, buf, n);
}

/**
 * @brief Bulk read bytes from flash after command/address framing.
 *
 * Command-sized payloads stay on the polled path; larger data phases use the
 * stream helper so DMA-backed targets avoid byte-at-a-time payload transfers.
 */
static inline bool tagStorageSpiBlockRead(const TagSpiDevice *device,
                                          uint8_t *buf, uint32_t n)
{
  if (n <= TAG_SPI_POLLED_TRANSFER_MAX)
    return tagSpiPolledReceive(device, buf, n);
  return tagSpiRead(device, buf, n);
}

/**
 * @brief Fill a 24-bit flash address in command byte order.
 *
 * @param[out] buf Destination address buffer; must hold at least 3 bytes.
 * @param[in] address 24-bit flash address.
 */
static inline void tagStorageSpiFillAddress(uint8_t *buf, uint32_t address)
{
  buf[0] = address >> 16;
  buf[1] = address >> 8;
  buf[2] = address & 0xff;
}

/**
 * @brief Fill a flash command plus 24-bit address header.
 *
 * @param[out] buf Destination header buffer; must hold at least 4 bytes.
 * @param[in] cmd Flash command byte.
 * @param[in] address 24-bit flash address.
 */
static inline void tagStorageSpiFillCommandAddress(uint8_t *buf, uint8_t cmd,
                                                   uint32_t address)
{
  buf[0] = cmd;
  tagStorageSpiFillAddress(&buf[1], address);
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
  uint8_t header[4];
  bool ok;

  tagStorageSpiFillCommandAddress(header, cmd, address);
  tagSpiSelect(device);
  ok = tagStorageSpiWrite(device, header, sizeof(header));
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
       tagStorageSpiBlockRead(device, buf, num);
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
  uint8_t header[4];
  bool ok;

  tagStorageSpiFillCommandAddress(header, cmd, address);
  tagSpiSelect(device);
  ok = tagStorageSpiWrite(device, header, sizeof(header)) &&
       tagStorageSpiBlockRead(device, buf, num);
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
  uint8_t header[4];
  bool ok;

  tagStorageSpiFillCommandAddress(header, cmd, address);
  tagSpiSelect(device);
  ok = tagStorageSpiWrite(device, header, sizeof(header)) &&
       tagStorageSpiBlockWrite(device, buf, num);
  tagSpiDeselect(device);
  return ok;
}
/** @} */

#endif
