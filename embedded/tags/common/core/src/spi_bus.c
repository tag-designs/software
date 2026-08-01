/**
 * @file spi_bus.c
 * @brief SPI descriptor lifecycle, active tracking, and backend selector.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "spi_bus.h"

#include "core_runtime.h"
#include "core_sync.h"
#include "gpio_utils.h"
#include "power.h"

#ifndef TAG_SPI_ERROR_BYTE
#define TAG_SPI_ERROR_BYTE 0xffU
#endif

#ifndef TAG_SPI_TRANSFER_STATUS
#define TAG_SPI_TRANSFER_STATUS 0
#endif

#if (defined(STM32_SPI_USE_SPI1) && STM32_SPI_USE_SPI1) ||                  \
    (defined(STM32_SPI_USE_SPI2) && STM32_SPI_USE_SPI2) ||                  \
    (defined(STM32_SPI_USE_SPI3) && STM32_SPI_USE_SPI3) ||                  \
    (defined(STM32_SPI_USE_SPI4) && STM32_SPI_USE_SPI4) ||                  \
    (defined(STM32_SPI_USE_SPI5) && STM32_SPI_USE_SPI5) ||                  \
    (defined(STM32_SPI_USE_SPI6) && STM32_SPI_USE_SPI6)
#define TAG_SPI_TRACKING_ENABLED 1
#else
#define TAG_SPI_TRACKING_ENABLED 0
#endif

#if defined(STM32_SPI_USE_SPI1) && STM32_SPI_USE_SPI1
static bool spi1_on = false;
static bool spi1_suspended_for_stop = false;
#endif
#if defined(STM32_SPI_USE_SPI2) && STM32_SPI_USE_SPI2
static bool spi2_on = false;
static bool spi2_suspended_for_stop = false;
#endif
#if defined(STM32_SPI_USE_SPI3) && STM32_SPI_USE_SPI3
static bool spi3_on = false;
static bool spi3_suspended_for_stop = false;
#endif
#if defined(STM32_SPI_USE_SPI4) && STM32_SPI_USE_SPI4
static bool spi4_on = false;
static bool spi4_suspended_for_stop = false;
#endif
#if defined(STM32_SPI_USE_SPI5) && STM32_SPI_USE_SPI5
static bool spi5_on = false;
static bool spi5_suspended_for_stop = false;
#endif
#if defined(STM32_SPI_USE_SPI6) && STM32_SPI_USE_SPI6
static bool spi6_on = false;
static bool spi6_suspended_for_stop = false;
#endif

static bool *tagSpiOnFlag(SPI_TypeDef *spi)
{
  (void)spi;

#if defined(STM32_SPI_USE_SPI1) && STM32_SPI_USE_SPI1
  if (spi == SPI1)
    return &spi1_on;
#endif
#if defined(STM32_SPI_USE_SPI2) && STM32_SPI_USE_SPI2
  if (spi == SPI2)
    return &spi2_on;
#endif
#if defined(STM32_SPI_USE_SPI3) && STM32_SPI_USE_SPI3
  if (spi == SPI3)
    return &spi3_on;
#endif
#if defined(STM32_SPI_USE_SPI4) && STM32_SPI_USE_SPI4
  if (spi == SPI4)
    return &spi4_on;
#endif
#if defined(STM32_SPI_USE_SPI5) && STM32_SPI_USE_SPI5
  if (spi == SPI5)
    return &spi5_on;
#endif
#if defined(STM32_SPI_USE_SPI6) && STM32_SPI_USE_SPI6
  if (spi == SPI6)
    return &spi6_on;
#endif
  return 0;
}

static bool *tagSpiSuspendedForStopFlag(SPI_TypeDef *spi)
{
  (void)spi;

#if defined(STM32_SPI_USE_SPI1) && STM32_SPI_USE_SPI1
  if (spi == SPI1)
    return &spi1_suspended_for_stop;
#endif
#if defined(STM32_SPI_USE_SPI2) && STM32_SPI_USE_SPI2
  if (spi == SPI2)
    return &spi2_suspended_for_stop;
#endif
#if defined(STM32_SPI_USE_SPI3) && STM32_SPI_USE_SPI3
  if (spi == SPI3)
    return &spi3_suspended_for_stop;
#endif
#if defined(STM32_SPI_USE_SPI4) && STM32_SPI_USE_SPI4
  if (spi == SPI4)
    return &spi4_suspended_for_stop;
#endif
#if defined(STM32_SPI_USE_SPI5) && STM32_SPI_USE_SPI5
  if (spi == SPI5)
    return &spi5_suspended_for_stop;
#endif
#if defined(STM32_SPI_USE_SPI6) && STM32_SPI_USE_SPI6
  if (spi == SPI6)
    return &spi6_suspended_for_stop;
#endif
  return 0;
}

#if TAG_SPI_TRACKING_ENABLED
static void tagSpiDisableForStop(SPI_TypeDef *spi)
{
  bool *on = tagSpiOnFlag(spi);
  bool *suspended = tagSpiSuspendedForStopFlag(spi);

  if (on && suspended && *on) {
    spi->CR1 &= ~SPI_CR1_SPE;
    *suspended = true;
  }
}

static void tagSpiEnableAfterStop(SPI_TypeDef *spi)
{
  bool *suspended = tagSpiSuspendedForStopFlag(spi);

  if (suspended && *suspended) {
    spi->CR1 |= SPI_CR1_SPE;
    *suspended = false;
  }
}
#endif

bool tagSpiIsOn(SPI_TypeDef *spi)
{
  bool *on = tagSpiOnFlag(spi);

  return on && *on;
}

void tagMarkSpiOn(SPI_TypeDef *spi)
{
  bool *on = tagSpiOnFlag(spi);

  if (on)
    *on = true;
}

void tagMarkSpiOff(SPI_TypeDef *spi)
{
  bool *on = tagSpiOnFlag(spi);
  bool *suspended = tagSpiSuspendedForStopFlag(spi);

  if (on)
    *on = false;
  if (suspended)
    *suspended = false;
}

void tagSpiDisableActiveForStop(void)
{
#if defined(STM32_SPI_USE_SPI1) && STM32_SPI_USE_SPI1
  tagSpiDisableForStop(SPI1);
#endif
#if defined(STM32_SPI_USE_SPI2) && STM32_SPI_USE_SPI2
  tagSpiDisableForStop(SPI2);
#endif
#if defined(STM32_SPI_USE_SPI3) && STM32_SPI_USE_SPI3
  tagSpiDisableForStop(SPI3);
#endif
#if defined(STM32_SPI_USE_SPI4) && STM32_SPI_USE_SPI4
  tagSpiDisableForStop(SPI4);
#endif
#if defined(STM32_SPI_USE_SPI5) && STM32_SPI_USE_SPI5
  tagSpiDisableForStop(SPI5);
#endif
#if defined(STM32_SPI_USE_SPI6) && STM32_SPI_USE_SPI6
  tagSpiDisableForStop(SPI6);
#endif
}

void tagSpiEnableActiveAfterStop(void)
{
#if defined(STM32_SPI_USE_SPI1) && STM32_SPI_USE_SPI1
  tagSpiEnableAfterStop(SPI1);
#endif
#if defined(STM32_SPI_USE_SPI2) && STM32_SPI_USE_SPI2
  tagSpiEnableAfterStop(SPI2);
#endif
#if defined(STM32_SPI_USE_SPI3) && STM32_SPI_USE_SPI3
  tagSpiEnableAfterStop(SPI3);
#endif
#if defined(STM32_SPI_USE_SPI4) && STM32_SPI_USE_SPI4
  tagSpiEnableAfterStop(SPI4);
#endif
#if defined(STM32_SPI_USE_SPI5) && STM32_SPI_USE_SPI5
  tagSpiEnableAfterStop(SPI5);
#endif
#if defined(STM32_SPI_USE_SPI6) && STM32_SPI_USE_SPI6
  tagSpiEnableAfterStop(SPI6);
#endif
}

static void tagSpiFillReadFailure(uint8_t *buf, uint32_t len)
{
  while (len--)
    *buf++ = TAG_SPI_ERROR_BYTE;
}

static void tagSpiSetChipSelectActive(const TagSpiDevice *device);
static void tagSpiSetChipSelectIdle(const TagSpiDevice *device);

#if defined(HAL_USE_SPI) && (HAL_USE_SPI == TRUE)
#include "spi_bus_chibios.inc"
#else
#include "spi_bus_polled.inc"
#endif

static void tagSpiBusAcquire(const TagSpiDevice *device)
{
#if defined(HAL_USE_SPI) && (HAL_USE_SPI == TRUE) &&                         \
    defined(SPI_USE_MUTUAL_EXCLUSION) && (SPI_USE_MUTUAL_EXCLUSION == TRUE)
  SPIDriver *driver = tagSpiDeviceDriver(device);

  if (driver != NULL)
    spiAcquireBus(driver);
#else
  if (device->mutex)
    chBSemWait(device->mutex);
#endif
}

static void tagSpiBusRelease(const TagSpiDevice *device)
{
#if defined(HAL_USE_SPI) && (HAL_USE_SPI == TRUE) &&                         \
    defined(SPI_USE_MUTUAL_EXCLUSION) && (SPI_USE_MUTUAL_EXCLUSION == TRUE)
  SPIDriver *driver = tagSpiDeviceDriver(device);

  if (driver != NULL)
    spiReleaseBus(driver);
#else
  if (device->mutex)
    chBSemSignal(device->mutex);
#endif
}

/**
 * @brief Drive a valid GPIO line high as a push-pull output.
 *
 * @param[in] line Board line to drive.
 */
static void tagSpiSetLineOutputHigh(ioline_t line)
{
  if (tagLineIsValid(line)) {
    palSetLine(line);
    palSetLineMode(line, PAL_MODE_OUTPUT_PUSHPULL);
  }
}

/**
 * @brief Drive a valid GPIO line low as a push-pull output.
 *
 * @param[in] line Board line to drive.
 */
static void tagSpiSetLineOutputLow(ioline_t line)
{
  if (tagLineIsValid(line)) {
    palClearLine(line);
    palSetLineMode(line, PAL_MODE_OUTPUT_PUSHPULL);
  }
}

/**
 * @brief Put a valid GPIO line into analog input mode.
 *
 * @param[in] line Board line to release.
 */
static void tagSpiSetLineAnalog(ioline_t line)
{
  if (tagLineIsValid(line)) {
    palSetLineMode(line, PAL_MODE_INPUT_ANALOG);
  }
}

/**
 * @brief Deassert a device chip-select line.
 *
 * @param[in] device SPI device descriptor whose CS line should idle inactive.
 */
static void tagSpiSetChipSelectIdle(const TagSpiDevice *device)
{
  tagSpiSetLineOutputHigh(device->config.ssline);
}

/**
 * @brief Assert a device chip-select line.
 *
 * @param[in] device SPI device descriptor whose CS line should be active.
 */
static void tagSpiSetChipSelectActive(const TagSpiDevice *device)
{
  tagSpiSetLineOutputLow(device->config.ssline);
}

/**
 * @brief Put shared SPI bus pins into the common inactive drive state.
 *
 * @details SCK and MOSI are safe to drive low when no bus session is active.
 *          MISO remains high-Z because it is driven by the selected device.
 *          The operation is intentionally idempotent for devices sharing the
 *          same SPI bus pins.
 *
 * @param[in] device SPI device descriptor naming the shared bus pins.
 */
static void tagSpiSetBusIdle(const TagSpiDevice *device)
{
  tagSpiSetLineOutputLow(device->sck);
  tagSpiSetLineOutputLow(device->mosi);
  tagSpiSetLineAnalog(device->miso);
}

void tagSpiDevicePowerOn(const TagSpiDevice *device)
{
  if (tagLineIsValid(device->pwr)) {
    palSetLine(device->pwr);
    palSetLineMode(device->pwr, PAL_MODE_OUTPUT_PUSHPULL);
  }

  tagSpiSetChipSelectIdle(device);
}

void tagSpiDevicePowerOff(const TagSpiDevice *device)
{
  tagSpiSetChipSelectIdle(device);
  tagSpiSetBusIdle(device);

  if (tagLineIsValid(device->pwr)) {
    palClearLine(device->pwr);
  }
}

void tagSpiBusBegin(const TagSpiDevice *device)
{
  tagSpiBusAcquire(device);

  tagSpiSetChipSelectIdle(device);
  palSetLineMode(device->sck, PAL_MODE_ALTERNATE(device->alternate_function) |
                                  PAL_STM32_OSPEED_MID2);
  palSetLineMode(device->miso, PAL_MODE_ALTERNATE(device->alternate_function) |
                                   PAL_STM32_OSPEED_MID2);
  palSetLineMode(device->mosi, PAL_MODE_ALTERNATE(device->alternate_function) |
                                   PAL_STM32_OSPEED_MID2);

  tagSpiDeviceEnable(device);
}

void tagSpiBusEnd(const TagSpiDevice *device)
{
  tagSpiSetChipSelectIdle(device);

  tagSpiDeviceDisable(device);

  tagSpiSetBusIdle(device);

  tagSpiBusRelease(device);
}

void tagSpiDevicePrepareSleep(const TagSpiDevice *device)
{
  tagSpiSetChipSelectIdle(device);
  tagEnableStandbyPullup(device->config.ssline);

  switch (device->sleep_policy) {
  case TAG_SPI_SLEEP_SAFE_IDLE:
    tagEnableStandbyPulldown(device->sck);
    tagEnableStandbyPulldown(device->mosi);
    break;

  case TAG_SPI_SLEEP_FLOAT:
    tagEnableStandbyPulldown(device->pwr);
    break;

  case TAG_SPI_SLEEP_CUSTOM:
    break;
  }
}
