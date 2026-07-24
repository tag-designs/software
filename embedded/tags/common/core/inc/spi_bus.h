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

#ifndef TAG_SPI_POLLED_TRANSFER_MAX
#define TAG_SPI_POLLED_TRANSFER_MAX 9U
#endif

#ifndef TAG_SPI_ERROR_BYTE
#define TAG_SPI_ERROR_BYTE 0xffU
#endif

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
#if defined(HAL_USE_SPI) && (HAL_USE_SPI == TRUE)
  SPIConfig spi;
#else
  uint32_t cr1;
  uint32_t cr2;
#endif
  ioline_t ssline;
} TagSpiConfig;

#if defined(HAL_USE_SPI) && (HAL_USE_SPI == TRUE)
#define TAG_SPI_USES_CHIBIOS_CONFIG 1
#else
#define TAG_SPI_USES_CHIBIOS_CONFIG 0
#endif

/* The HAL-backed SPI implementation is intentionally limited to ChibiOS' SPI
 * v2 driver model. STM32L432 and STM32U3 both select that API; the remaining
 * config variation is chip-select mode and STM32 LLD register layout.
 */
#if TAG_SPI_USES_CHIBIOS_CONFIG
#if !defined(HAL_LLD_SELECT_SPI_V2) || (HAL_LLD_SELECT_SPI_V2 != TRUE)
#error "Tag SPI ChibiOS backend requires the ChibiOS SPI v2 driver model"
#endif
#endif

/* Present only on ChibiOS SPI drivers with circular-transfer support. The
 * shared tag SPI descriptors use linear transfers by default.
 */
#if TAG_SPI_USES_CHIBIOS_CONFIG && (SPI_SUPPORTS_CIRCULAR == TRUE)
#define TAG_SPI_CONFIG_CIRCULAR_FIELD .circular = false,
#else
#define TAG_SPI_CONFIG_CIRCULAR_FIELD
#endif

/* Present on v2 drivers with slave-mode support. Tag peripherals are opened as
 * SPI masters unless a device-specific descriptor overrides the config.
 */
#if TAG_SPI_USES_CHIBIOS_CONFIG && (SPI_SUPPORTS_SLAVE_MODE == TRUE)
#define TAG_SPI_CONFIG_SLAVE_FIELD .slave = false,
#else
#define TAG_SPI_CONFIG_SLAVE_FIELD
#endif

/* Default descriptors use the synchronous SPI v2 APIs, so data and error
 * callbacks are intentionally disabled.
 */
#if TAG_SPI_USES_CHIBIOS_CONFIG
#define TAG_SPI_CONFIG_CALLBACK_FIELDS .data_cb = NULL, .error_cb = NULL,
#else
#define TAG_SPI_CONFIG_CALLBACK_FIELDS
#endif

/* ChibiOS supports several chip-select representations. Keep the public tag
 * descriptor line-based, then adapt it to the configured HAL SPI select mode.
 */
#if TAG_SPI_USES_CHIBIOS_CONFIG &&                                          \
    (SPI_SELECT_MODE == SPI_SELECT_MODE_LINE)
#define TAG_SPI_CONFIG_SELECT_FIELDS(SSLINE) .ssline = (SSLINE),
#elif TAG_SPI_USES_CHIBIOS_CONFIG &&                                        \
    (SPI_SELECT_MODE == SPI_SELECT_MODE_PORT)
#define TAG_SPI_CONFIG_SELECT_FIELDS(SSLINE)                                \
  .ssport = PAL_PORT(SSLINE),                                               \
  .ssmask = (ioportmask_t)(1U << PAL_PAD(SSLINE)),
#elif TAG_SPI_USES_CHIBIOS_CONFIG &&                                        \
    (SPI_SELECT_MODE == SPI_SELECT_MODE_PAD)
#define TAG_SPI_CONFIG_SELECT_FIELDS(SSLINE)                                \
  .ssport = PAL_PORT(SSLINE), .sspad = PAL_PAD(SSLINE),
#else
#define TAG_SPI_CONFIG_SELECT_FIELDS(SSLINE)
#endif

/* STM32U3 uses ChibiOS' STM32 SPIv4 LLD, whose SPIConfig carries CFG and DMA
 * tuning fields. The L432 SPIv2 LLD uses the older CR1/CR2 field pair.
 */
#if TAG_SPI_USES_CHIBIOS_CONFIG && defined(STM32U3XX)
#define TAG_SPI_CONFIG_LLD_FIELDS                                           \
  .cfg1 = SPI_CFG1_DSIZE_VALUE(7U), .cfg2 = 0U, .dtr1rx = 0U,               \
  .dtr1tx = 0U, .dtr2rx = 0U, .dtr2tx = 0U
#elif TAG_SPI_USES_CHIBIOS_CONFIG && defined(SPI_CR2_DS_2)
#define TAG_SPI_CONFIG_LLD_FIELDS                                           \
  .cr1 = 0U, .cr2 = SPI_CR2_DS_2 | SPI_CR2_DS_1 | SPI_CR2_DS_0
#elif TAG_SPI_USES_CHIBIOS_CONFIG
#error "Unsupported STM32 SPI LLD config shape for tag SPI default config"
#else
#define TAG_SPI_CONFIG_LLD_FIELDS
#endif

/* Complete ChibiOS SPIConfig initializer body, in the order expected by
 * ChibiOS' SPIConfig struct for the selected HAL and LLD.
 */
#define TAG_SPI_CHIBIOS_CONFIG_FIELDS(SSLINE)                               \
  TAG_SPI_CONFIG_CIRCULAR_FIELD                                             \
  TAG_SPI_CONFIG_SLAVE_FIELD                                                \
  TAG_SPI_CONFIG_CALLBACK_FIELDS                                            \
  TAG_SPI_CONFIG_SELECT_FIELDS(SSLINE)                                      \
  TAG_SPI_CONFIG_LLD_FIELDS

/* Direct-register polled mode keeps only the STM32 CR1/CR2 settings needed to
 * start an 8-bit master session plus the shared chip-select line.
 */
#if defined(SPI_CR1_MSTR)
#define TAG_SPI_POLLED_CONFIG_FIELDS(SSLINE)                                \
  .cr1 = SPI_CR1_MSTR,                                                      \
  .cr2 = SPI_CR2_FRXTH | SPI_CR2_SSOE | SPI_CR2_DS_2 | SPI_CR2_DS_1 |       \
         SPI_CR2_DS_0,                                                      \
  .ssline = (SSLINE)
#else
#define TAG_SPI_POLLED_CONFIG_FIELDS(SSLINE)                                \
  .cr1 = 0U, .cr2 = 0U, .ssline = (SSLINE)
#endif

/* Public default descriptor config. Its shape follows TagSpiConfig: either a
 * nested ChibiOS SPIConfig or the direct-register polled fields.
 */
#if TAG_SPI_USES_CHIBIOS_CONFIG
#define TAGSPIDEFAULTCONFIG(SSLINE)                                         \
  (TagSpiConfig){                                                           \
      .spi = {TAG_SPI_CHIBIOS_CONFIG_FIELDS(SSLINE)},                       \
      .ssline = (SSLINE)}
#else
#define TAGSPIDEFAULTCONFIG(SSLINE)                                         \
  (TagSpiConfig){TAG_SPI_POLLED_CONFIG_FIELDS(SSLINE)}
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
  ioline_t sck;
  ioline_t miso;
  ioline_t mosi;
  ioline_t pwr;
  uint8_t dummy;
  TagSpiSleepPolicy sleep_policy;
} TagSpiDevice;

//extern const TagSpiConfig tagSpiDefaultConfig;

#define TAG_SPI1_DEVICE_DEFAULTS(SSLINE)                                    \
  .spi = SPI1,                                                              \
  .mutex = &SPI1mutex,                                                      \
  .config = TAGSPIDEFAULTCONFIG(SSLINE),                                    \
  .alternate_function = SPI1_ALTERNATE_FUNCTION

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
 * @brief Exchange bytes full-duplex.
 *
 * @param[in] device SPI device descriptor.
 * @param[in] txbuf Bytes to transmit.
 * @param[out] rxbuf Buffer that receives bytes from the device.
 * @param[in] len Number of bytes to exchange.
 */
bool tagSpiExchange(const TagSpiDevice *device, const uint8_t *txbuf,
                    uint8_t *rxbuf, uint32_t len);

#if TAG_SPI_USES_CHIBIOS_CONFIG
static inline SPIDriver *tagSpiPolledDeviceDriver(const TagSpiDevice *device)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);

#if defined(STM32_SPI_USE_SPI1) && STM32_SPI_USE_SPI1
  if (spi == SPI1)
    return &SPID1;
#endif
#if defined(STM32_SPI_USE_SPI2) && STM32_SPI_USE_SPI2
  if (spi == SPI2)
    return &SPID2;
#endif
#if defined(STM32_SPI_USE_SPI3) && STM32_SPI_USE_SPI3
  if (spi == SPI3)
    return &SPID3;
#endif
#if defined(STM32_SPI_USE_SPI4) && STM32_SPI_USE_SPI4
  if (spi == SPI4)
    return &SPID4;
#endif
#if defined(STM32_SPI_USE_SPI5) && STM32_SPI_USE_SPI5
  if (spi == SPI5)
    return &SPID5;
#endif
#if defined(STM32_SPI_USE_SPI6) && STM32_SPI_USE_SPI6
  if (spi == SPI6)
    return &SPID6;
#endif

  return NULL;
}

/**
 * @brief Exchange one SPI byte through ChibiOS' polled SPI path.
 *
 * @param[in] device SPI device descriptor.
 * @param[in] tx Byte to transmit.
 * @param[out] rx Optional received byte; pass NULL to discard it.
 * @return true on successful exchange.
 */
static inline bool tagSpiPolledExchange(const TagSpiDevice *device, uint8_t tx,
                                        uint8_t *rx)
{
  SPIDriver *driver = tagSpiPolledDeviceDriver(device);
  uint8_t value;

  if (driver == NULL) {
    if (rx != NULL)
      *rx = TAG_SPI_ERROR_BYTE;
    return false;
  }

  value = (uint8_t)spiPolledExchange(driver, tx);
  if (rx != NULL)
    *rx = value;
  return true;
}

/**
 * @brief Send bytes through ChibiOS' polled SPI path and discard RX.
 *
 * @param[in] device SPI device descriptor.
 * @param[in] buf Bytes to transmit.
 * @param[in] len Number of bytes to transmit.
 * @return true on successful transfer.
 */
static inline bool tagSpiPolledSend(const TagSpiDevice *device,
                                    const uint8_t *buf, uint32_t len)
{
  SPIDriver *driver = tagSpiPolledDeviceDriver(device);

  if (driver == NULL)
    return false;

  while (len-- > 0U)
    (void)spiPolledExchange(driver, *buf++);
  return true;
}

/**
 * @brief Receive bytes through ChibiOS' polled SPI path.
 *
 * @param[in] device SPI device descriptor.
 * @param[out] buf Buffer that receives bytes from the device.
 * @param[in] len Number of bytes to receive.
 * @return true on successful transfer.
 */
static inline bool tagSpiPolledReceive(const TagSpiDevice *device,
                                       uint8_t *buf, uint32_t len)
{
  SPIDriver *driver = tagSpiPolledDeviceDriver(device);

  if (driver == NULL) {
    while (len-- > 0U)
      *buf++ = TAG_SPI_ERROR_BYTE;
    return false;
  }

  while (len-- > 0U)
    *buf++ = (uint8_t)spiPolledExchange(driver, device->dummy);
  return true;
}
#else
bool tagSpiPolledExchange(const TagSpiDevice *device, uint8_t tx, uint8_t *rx);
bool tagSpiPolledSend(const TagSpiDevice *device, const uint8_t *buf,
                      uint32_t len);
bool tagSpiPolledReceive(const TagSpiDevice *device, uint8_t *buf,
                         uint32_t len);
#endif

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

/**
 * @brief Exchange one complete SPI transaction under chip select.
 *
 * @param[in] device SPI device descriptor.
 * @param[in] txbuf Bytes to transmit.
 * @param[out] rxbuf Buffer that receives bytes from the device.
 * @param[in] len Number of bytes to exchange.
 */
static inline bool tagSpiBusExchange(const TagSpiDevice *device,
                                     const uint8_t *txbuf, uint8_t *rxbuf,
                                     uint32_t len)
{
  bool ok;

  tagSpiSelect(device);
  ok = tagSpiExchange(device, txbuf, rxbuf, len);
  tagSpiDeselect(device);
  return ok;
}
/** @} */
/** @} */

#endif
