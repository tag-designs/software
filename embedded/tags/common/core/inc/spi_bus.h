/**
 * @file spi_bus.h
 * @brief SPI device descriptors, lifecycle helpers, and raw byte transfers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_SPI_BUS_H
#define TAG_CORE_SPI_BUS_H

#include "core_sync.h"

#include "hal.h"

#include <stdbool.h>
#include <stdint.h>


// alternate function number for spi -- stm32l432

#define SPI1_ALTERNATE_FUNCTION 5


/** @name SPI device model
 * SPI bus helpers.
 *
 * Core owns peripheral setup, active-state tracking, raw byte transfers, and
 * standby pin policy for SPI-backed devices. Device drivers should describe
 * register protocols through sensor_io/storage helpers rather than poking the
 * peripheral directly.
 * @{
 */

/** Register settings used when a device opens a SPI bus session. */
typedef struct {
  uint32_t cr1;
  uint32_t cr2;
  ioline_t ssline;
} TagSpiConfig;

#if defined(SPI_CR1_MSTR) 
   #define TAGSPIDEFAULTCONFIG(SSLINE) \
         (TagSpiConfig){ .cr1 = SPI_CR1_MSTR, \
                         .cr2 = SPI_CR2_FRXTH | SPI_CR2_SSOE | \
                                SPI_CR2_DS_2 | SPI_CR2_DS_1 | SPI_CR2_DS_0, \
                        .ssline = SSLINE }
#else
   #define TAGSPIDEFAULTCONFIG(SSLINE) \
          (TagSpiConfig){ .cr1 = 0, .cr2 = 0, .ssline = SSLINE }
#endif

/** Standby pull policy applied while preparing the MCU for deep sleep. */
typedef enum {
  TAG_SPI_SLEEP_FLOAT,
  TAG_SPI_SLEEP_SAFE_IDLE,
  TAG_SPI_SLEEP_CUSTOM
} TagSpiSleepPolicy;

/** Board-line description for one SPI device attached to a hardware peripheral. */
typedef struct {
  SPI_TypeDef *spi;
  binary_semaphore_t *mutex;
  int alternate_function;
  const TagSpiConfig config;
  //ioline_t cs;
  ioline_t sck;
  ioline_t miso;
  ioline_t mosi;
  ioline_t pwr;
  uint8_t dummy;
  TagSpiSleepPolicy sleep_policy;
} TagSpiDevice;

//extern const TagSpiConfig tagSpiDefaultConfig;

#define TAG_SPI1_DEVICE_DEFAULTS(SSLINE)                                             \
  .spi = SPI1, .mutex = &SPI1mutex, .config = TAGSPIDEFAULTCONFIG(SSLINE), .alternate_function = SPI1_ALTERNATE_FUNCTION

/** @} */

/** @name SPI active-state tracking
 * Tracking hooks let Stop2 suspend only the SPI peripherals that were active,
 * then restore them afterward without changing device power or chip select.
 * @{
 */
/**
 * @brief Report whether an SPI peripheral is currently enabled by a bus session.
 *
 * @param[in] spi STM32 SPI peripheral to inspect.
 * @return true when the peripheral is tracked as active.
 */
bool tagSpiIsOn(SPI_TypeDef *spi);

/**
 * @brief Mark an SPI peripheral active for Stop2 suspend tracking.
 *
 * @param[in] spi STM32 SPI peripheral to mark active.
 */
void tagMarkSpiOn(SPI_TypeDef *spi);

/**
 * @brief Mark an SPI peripheral inactive and clear any suspended state.
 *
 * @param[in] spi STM32 SPI peripheral to mark inactive.
 */
void tagMarkSpiOff(SPI_TypeDef *spi);

/**
 * @brief Disable currently active SPI peripherals before Stop2 entry.
 */
void tagSpiDisableActiveForStop(void);

/**
 * @brief Re-enable SPI peripherals suspended by Stop2 entry.
 */
void tagSpiEnableActiveAfterStop(void);
/** @} */

/** @name SPI descriptor access
 * Accessors keep raw register users tied to a descriptor rather than separate
 * global peripheral assumptions.
 * @{
 */
/**
 * @brief Return the STM32 peripheral referenced by an SPI device descriptor.
 *
 * @param[in] device SPI device descriptor.
 * @return STM32 SPI peripheral for the device.
 */
static inline SPI_TypeDef *tagSpiDevicePeripheral(const TagSpiDevice *device)
{
  return device->spi;
}
/** @} */

/** @name SPI device power
 * Device power controls the optional switched power line and safe idle pin
 * states for the device. It does not start or stop the MCU SPI peripheral.
 * @{
 */
/**
 * @brief Assert device power and put chip select into a safe idle state.
 *
 * @param[in] device SPI device descriptor to power.
 */
void tagSpiDevicePowerOn(const TagSpiDevice *device);

/**
 * @brief Remove device power and return SPI pins to analog low-leakage mode.
 *
 * @param[in] device SPI device descriptor to power down.
 */
void tagSpiDevicePowerOff(const TagSpiDevice *device);
/** @} */

/** @name SPI bus sessions
 * Bus sessions enable or disable the MCU SPI peripheral using the device's
 * configuration. Callers normally power the device first, then begin the bus;
 * shutdown happens in the reverse order.
 * @{
 */
/**
 * @brief Claim the shared bus and enable the SPI peripheral for this device.
 *
 * @param[in] device SPI device descriptor whose session should begin.
 */
void tagSpiBusBegin(const TagSpiDevice *device);

/**
 * @brief Disable the SPI peripheral and release the shared bus.
 *
 * @param[in] device SPI device descriptor whose session should end.
 */
void tagSpiBusEnd(const TagSpiDevice *device);
/** @} */

/** @name SPI low-power preparation
 * Apply the device's standby pull policy before entering low-power stop or
 * standby states. This is separate from normal bus-session teardown.
 * @{
 */
/**
 * @brief Apply standby pull policy for an SPI-backed device.
 *
 * @param[in] device SPI device descriptor whose sleep policy should be applied.
 */
void tagSpiDevicePrepareSleep(const TagSpiDevice *device);
/** @} */

/** @name SPI raw transfers
 * Byte-transfer helpers used by register and storage adapters once a bus
 * session and chip-select phase have been established.
 * @{
 */
/**
 * @brief Write bytes and discard the full-duplex response stream.
 *
 * @param[in] device SPI device descriptor.
 * @param[in] buf Bytes to transmit.
 * @param[in] len Number of bytes to transmit.
 */
bool tagSpiWrite(const TagSpiDevice *device, const uint8_t *buf, uint32_t len);

/**
 * @brief Read bytes by clocking the descriptor's dummy byte.
 *
 * @param[in] device SPI device descriptor.
 * @param[out] buf Buffer that receives bytes from the device.
 * @param[in] len Number of bytes to read.
 */
bool tagSpiRead(const TagSpiDevice *device, uint8_t *buf, uint32_t len);

/**
 * @brief Read bytes using a DMA-backed dummy-byte transfer when available.
 *
 * @param[in] device SPI device descriptor.
 * @param[out] buf Buffer that receives bytes from the device.
 * @param[in] len Number of bytes to read.
 * @return true when DMA completed the transfer, false when DMA is unavailable
 *         or the transfer failed.
 */
bool tagSpiReadDma(const TagSpiDevice *device, uint8_t *buf, uint32_t len);

/**
 * @brief Pipelined SPI write for devices proven safe with queued transfers.
 *
 * @param[in] device SPI device descriptor.
 * @param[in] buf Bytes to transmit.
 * @param[in] len Number of bytes to transmit.
 */
bool tagSpiWritePipelined(const TagSpiDevice *device, const uint8_t *buf,
                          uint32_t len);

/**
 * @brief Pipelined SPI read for devices proven safe with queued transfers.
 *
 * @param[in] device SPI device descriptor.
 * @param[out] buf Buffer that receives bytes from the device.
 * @param[in] len Number of bytes to read.
 */
bool tagSpiReadPipelined(const TagSpiDevice *device, uint8_t *buf,
                         uint32_t len);

/**
 * @brief Assert the SPI device chip-select line.
 *
 * @param[in] device SPI device descriptor to select.
 */
void tagSpiSelect(const TagSpiDevice *device);

/**
 * @brief Deassert the SPI device chip-select line.
 *
 * @param[in] device SPI device descriptor to deselect.
 */
void tagSpiDeselect(const TagSpiDevice *device);

/**
 * Convenience transaction wrappers. These assert chip select, perform one raw
 * byte transfer, then release chip select. Use the lower-level select/write/read
 * calls directly when a device protocol needs multiple phases under one CS.
 * @{
 */
/**
 * @brief Write one complete SPI transaction under chip select.
 *
 * @param[in] device SPI device descriptor.
 * @param[in] buf Bytes to transmit.
 * @param[in] len Number of bytes to transmit.
 */
static inline bool tagSpiBusWrite(const TagSpiDevice *device,
                                  const uint8_t *buf, uint32_t len)
{
  bool ok;

  tagSpiSelect(device);
  ok = tagSpiWrite(device, buf, len);
  tagSpiDeselect(device);
  return ok;
}

/**
 * @brief Read one complete SPI transaction under chip select.
 *
 * @param[in] device SPI device descriptor.
 * @param[out] buf Buffer that receives bytes from the device.
 * @param[in] len Number of bytes to read.
 */
static inline bool tagSpiBusRead(const TagSpiDevice *device, uint8_t *buf,
                                 uint32_t len)
{
  bool ok;

  tagSpiSelect(device);
  ok = tagSpiRead(device, buf, len);
  tagSpiDeselect(device);
  return ok;
}
/** @} */
/** @} */

#endif
