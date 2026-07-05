/**
 * @file spi_bus.c
 * @brief SPI descriptor lifecycle, active tracking, and raw transfer helpers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "power.h"

#include "core_sync.h"
#include "gpio_utils.h"

#ifndef TAG_SPI_POLL_LIMIT
#define TAG_SPI_POLL_LIMIT 100000U
#endif

#ifndef TAG_SPI_ERROR_BYTE
#define TAG_SPI_ERROR_BYTE 0xffU
#endif

/** @name SPI bus implementation overview
 * Polling SPI byte transfers shared by simple peripheral drivers.
 *
 * Existing tag code drives SPI peripheral registers directly rather than
 * through ChibiOS SPI transactions, so these helpers preserve that behavior
 * while centralizing the repeated full-duplex drain/read loops. This file also
 * owns SPI bus-session pin state, shared peripheral configuration,
 * active-state tracking, and bus mutex for descriptor-backed SPI devices.
 *
 * Storage flash drivers can be more sensitive to command/address/data pacing.
 * They should use storage_spi.h when they need byte-at-a-time transfers with
 * chip select held across a complete flash transaction.
 * @{
 */

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
/** @} */

/** @name SPI active-state tracking
 * Active-peripheral tracking for Stop2.
 *
 * Short sleeps suspend active peripherals without changing device power,
 * chip-select ownership, or pin alternate-function setup.
 * @{
 */
/**
 * @brief Return the active-state flag for a configured SPI peripheral.
 *
 * @param[in] spi STM32 SPI peripheral to look up.
 * @return Pointer to the active flag, or NULL when the peripheral is not tracked.
 */
static bool *tagSpiOnFlag(SPI_TypeDef *spi)
{
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

/**
 * @brief Return the Stop2-suspended flag for a configured SPI peripheral.
 *
 * @param[in] spi STM32 SPI peripheral to look up.
 * @return Pointer to the suspended flag, or NULL when the peripheral is not tracked.
 */
static bool *tagSpiSuspendedForStopFlag(SPI_TypeDef *spi)
{
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

/**
 * @brief Enable and reset the RCC clock for a configured SPI peripheral.
 *
 * @param[in] spi STM32 SPI peripheral whose clock should be prepared.
 */
static void tagSpiPeripheralEnableClock(SPI_TypeDef *spi)
{
#if defined(STM32_SPI_USE_SPI1) && STM32_SPI_USE_SPI1
  if (spi == SPI1)
  {
    rccEnableSPI1(0);
    rccResetSPI1();
    return;
  }
#endif
#if defined(STM32_SPI_USE_SPI2) && STM32_SPI_USE_SPI2
  if (spi == SPI2)
  {
    rccEnableSPI2(0);
    rccResetSPI2();
    return;
  }
#endif
#if defined(STM32_SPI_USE_SPI3) && STM32_SPI_USE_SPI3
  if (spi == SPI3)
  {
    rccEnableSPI3(0);
    rccResetSPI3();
    return;
  }
#endif
#if defined(STM32_SPI_USE_SPI4) && STM32_SPI_USE_SPI4
  if (spi == SPI4)
  {
    rccEnableSPI4(0);
    rccResetSPI4();
    return;
  }
#endif
#if defined(STM32_SPI_USE_SPI5) && STM32_SPI_USE_SPI5
  if (spi == SPI5)
  {
    rccEnableSPI5(0);
    rccResetSPI5();
    return;
  }
#endif
#if defined(STM32_SPI_USE_SPI6) && STM32_SPI_USE_SPI6
  if (spi == SPI6)
  {
    rccEnableSPI6(0);
    rccResetSPI6();
    return;
  }
#endif
  (void)spi;
}

/**
 * @brief Disable a tracked active SPI peripheral for Stop2.
 *
 * @param[in] spi STM32 SPI peripheral to suspend if active.
 */
static void tagSpiDisableForStop(SPI_TypeDef *spi)
{
  bool *on = tagSpiOnFlag(spi);
  bool *suspended = tagSpiSuspendedForStopFlag(spi);

  if (on && suspended && *on)
  {
    spi->CR1 &= ~SPI_CR1_SPE;
    *suspended = true;
  }
}

/**
 * @brief Re-enable a tracked SPI peripheral that was suspended for Stop2.
 *
 * @param[in] spi STM32 SPI peripheral to resume if previously suspended.
 */
static void tagSpiEnableAfterStop(SPI_TypeDef *spi)
{
  bool *suspended = tagSpiSuspendedForStopFlag(spi);

  if (suspended && *suspended)
  {
    spi->CR1 |= SPI_CR1_SPE;
    *suspended = false;
  }
}

/**
 * @brief Report whether an SPI peripheral is currently enabled by a bus session.
 *
 * @param[in] spi STM32 SPI peripheral to inspect.
 * @return true when the peripheral is tracked as active.
 */
bool tagSpiIsOn(SPI_TypeDef *spi)
{
  bool *on = tagSpiOnFlag(spi);

  return on && *on;
}

/**
 * @brief Mark an SPI peripheral active for Stop2 suspend tracking.
 *
 * @param[in] spi STM32 SPI peripheral to mark active.
 */
void tagMarkSpiOn(SPI_TypeDef *spi)
{
  bool *on = tagSpiOnFlag(spi);

  if (on)
  {
    *on = true;
  }
}

/**
 * @brief Mark an SPI peripheral inactive and clear any suspended state.
 *
 * @param[in] spi STM32 SPI peripheral to mark inactive.
 */
void tagMarkSpiOff(SPI_TypeDef *spi)
{
  bool *on = tagSpiOnFlag(spi);
  bool *suspended = tagSpiSuspendedForStopFlag(spi);

  if (on)
  {
    *on = false;
  }
  if (suspended)
  {
    *suspended = false;
  }
}

/**
 * @brief Disable currently active SPI peripherals before Stop2 entry.
 */
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

/**
 * @brief Re-enable SPI peripherals suspended by Stop2 entry.
 */
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
/** @} */

/** @name SPI configuration and peripheral enable
 * Helpers that translate a compile-time device descriptor into the STM32 SPI
 * register state needed for one bus session.
 * @{
 */
#if (defined(STM32_SPI_USE_SPI1) && STM32_SPI_USE_SPI1) ||                         \
    (defined(STM32_SPI_USE_SPI2) && STM32_SPI_USE_SPI2) ||                         \
    (defined(STM32_SPI_USE_SPI3) && STM32_SPI_USE_SPI3) ||                         \
    (defined(STM32_SPI_USE_SPI4) && STM32_SPI_USE_SPI4) ||                         \
    (defined(STM32_SPI_USE_SPI5) && STM32_SPI_USE_SPI5) ||                         \
    (defined(STM32_SPI_USE_SPI6) && STM32_SPI_USE_SPI6)
const TagSpiConfig tagSpiDefaultConfig = {
#if defined(SPI_CR1_MSTR)
    .cr1 = SPI_CR1_MSTR,
    .cr2 = SPI_CR2_FRXTH | SPI_CR2_SSOE | SPI_CR2_DS_2 |
           SPI_CR2_DS_1 | SPI_CR2_DS_0,
#else
    .cr1 = 0,
    .cr2 = 0,
#endif
};

#else
const TagSpiConfig tagSpiDefaultConfig = {
    .cr1 = 0,
    .cr2 = 0,
};
#endif

/**
 * @brief Configure and enable the SPI peripheral for one device session.
 *
 * @param[in] device SPI device descriptor supplying peripheral and config.
 */
static void tagSpiDeviceEnable(const TagSpiDevice *device)
{
  const TagSpiConfig *config = device->config;
  if (!config)
  {
    config = &tagSpiDefaultConfig;
  }

  tagSpiPeripheralEnableClock(device->spi);

#if defined(SPI_TXDR_TXDR)
  device->spi->CR1 = 0;
  device->spi->CR2 = 0;
  device->spi->IER = 0;
  device->spi->IFCR = 0xFFFFFFFFU;
  device->spi->CFG1 = (7U << SPI_CFG1_DSIZE_Pos);
  device->spi->CFG2 = SPI_CFG2_MASTER | SPI_CFG2_SSOE;
  device->spi->CR1 = SPI_CR1_MASRX | SPI_CR1_SPE;
#else
  device->spi->CR1 = 0;
  device->spi->CR2 = config->cr2;
  device->spi->CR1 = config->cr1;
  device->spi->CR1 |= SPI_CR1_SPE;
#endif

  tagMarkSpiOn(device->spi);
}

/**
 * @brief Disable the SPI peripheral at the end of one device session.
 *
 * @param[in] device SPI device descriptor supplying the peripheral.
 */
static void tagSpiDeviceDisable(const TagSpiDevice *device)
{
#if defined(SPI_TXDR_TXDR)
  device->spi->CR1 = 0;
  device->spi->CR2 = 0;
  device->spi->IER = 0;
  device->spi->CFG1 = 0;
  device->spi->CFG2 = 0;
  device->spi->IFCR = 0xFFFFFFFFU;
#else
  device->spi->CR1 = 0;
  device->spi->CR2 = 0;
#endif

  tagMarkSpiOff(device->spi);
}
/** @} */

/** @name SPI device power and bus sessions
 * Device power and bus sessions.
 *
 * Power on/off handles optional switched power and safe idle pins. Bus
 * begin/end owns the shared mutex, alternate functions, and peripheral enable
 * state for one transaction/session.
 * @{
 */
/**
 * @brief Assert device power and put chip select into a safe idle state.
 *
 * @param[in] device SPI device descriptor to power.
 */
void tagSpiDevicePowerOn(const TagSpiDevice *device)
{
  if (tagLineIsValid(device->pwr))
  {
    palSetLine(device->pwr);
    palSetLineMode(device->pwr, PAL_MODE_OUTPUT_PUSHPULL);
  }

  palSetLine(device->cs);
  palSetLineMode(device->cs, PAL_MODE_OUTPUT_PUSHPULL);
}

/**
 * @brief Remove device power and return SPI pins to analog low-leakage mode.
 *
 * @param[in] device SPI device descriptor to power down.
 */
void tagSpiDevicePowerOff(const TagSpiDevice *device)
{
  palSetLine(device->cs);
  palSetLineMode(device->cs, PAL_MODE_OUTPUT_PUSHPULL);
  palSetLineMode(device->sck, PAL_MODE_INPUT_PULLDOWN);
  palSetLineMode(device->mosi, PAL_MODE_INPUT_PULLDOWN);
  palSetLineMode(device->miso, PAL_MODE_INPUT_PULLDOWN);
  
  if (tagLineIsValid(device->pwr))
  {
    palSetLineMode(device->cs, PAL_MODE_INPUT_ANALOG);
    palClearLine(device->pwr);
  } 
}

/**
 * @brief Claim the shared bus and enable the SPI peripheral for this device.
 *
 * @param[in] device SPI device descriptor whose session should begin.
 */
void tagSpiBusBegin(const TagSpiDevice *device)
{
  if (device->mutex)
  {
    chBSemWait(device->mutex);
  }

  palSetLine(device->cs);
  palSetLineMode(device->cs,  PAL_MODE_OUTPUT_PUSHPULL);
  palSetLineMode(device->sck, PAL_MODE_ALTERNATE(device->alternate_function) | PAL_STM32_OSPEED_MID2);
  palSetLineMode(device->miso, PAL_MODE_ALTERNATE(device->alternate_function) | PAL_STM32_OSPEED_MID2);
  palSetLineMode(device->mosi, PAL_MODE_ALTERNATE(device->alternate_function) | PAL_STM32_OSPEED_MID2);

  tagSpiDeviceEnable(device);
}

/**
 * @brief Disable the SPI peripheral and release the shared bus.
 *
 * @param[in] device SPI device descriptor whose session should end.
 */
void tagSpiBusEnd(const TagSpiDevice *device)
{
  palSetLine(device->cs);
  palClearLine(device->mosi);
  palClearLine(device->sck);

  tagSpiDeviceDisable(device);

  palSetLineMode(device->cs, PAL_MODE_OUTPUT_PUSHPULL);
  palSetLineMode(device->sck, PAL_MODE_OUTPUT_PUSHPULL);
  palSetLineMode(device->mosi, PAL_MODE_OUTPUT_PUSHPULL);
  palSetLineMode(device->miso, PAL_MODE_INPUT_PULLDOWN);

  if (device->mutex)
  {
    chBSemSignal(device->mutex);
  }
}

/**
 * @brief Apply standby pull policy for an SPI-backed device.
 *
 * @param[in] device SPI device descriptor whose sleep policy should be applied.
 */
void tagSpiDevicePrepareSleep(const TagSpiDevice *device)
{
  switch (device->sleep_policy)
  {
  case TAG_SPI_SLEEP_SAFE_IDLE:
    tagEnableStandbyPullup(device->cs);
    tagEnableStandbyPulldown(device->sck);
    tagEnableStandbyPulldown(device->mosi);
    tagEnableStandbyPulldown(device->miso);
    break;

  case TAG_SPI_SLEEP_FLOAT:
    tagEnableStandbyPulldown(device->pwr);
    break;

  case TAG_SPI_SLEEP_CUSTOM:
    break;
  }
}
/** @} */

/** @name SPI raw byte transfers
 * Raw byte transfers.
 *
 * The default read/write routines intentionally use byte-at-a-time
 * request/response loops. Pipelined variants remain available for cases that
 * have been proven safe on hardware.
 * @{
 */
static bool tagSpiHasTransferError(SPI_TypeDef *spi)
{
#if defined(SPI_SR_OVR)
  if ((spi->SR & SPI_SR_OVR) != 0U)
    return true;
#endif
#if defined(SPI_SR_MODF)
  if ((spi->SR & SPI_SR_MODF) != 0U)
    return true;
#endif
  return false;
}

static void tagSpiRecover(SPI_TypeDef *spi)
{
  volatile uint32_t dummy;
  uint32_t timeout = TAG_SPI_POLL_LIMIT;

  spi->CR1 &= ~SPI_CR1_SPE;

#if defined(SPI_TXDR_TXDR)
  while (((spi->SR & SPI_SR_RXP) != 0U) && (timeout-- > 0U))
    dummy = spi->RXDR;

  dummy = spi->RXDR;
  dummy = spi->SR;
  spi->IFCR = 0xFFFFFFFFU;
#else
  while (((spi->SR & SPI_SR_RXNE) != 0U) && (timeout-- > 0U))
    dummy = spi->DR;

  dummy = spi->DR;
  dummy = spi->SR;
#endif
  (void)dummy;

  spi->CR1 |= SPI_CR1_SPE;
}

static bool tagSpiWaitSet(SPI_TypeDef *spi, uint32_t mask)
{
  uint32_t timeout = TAG_SPI_POLL_LIMIT;

  while ((spi->SR & mask) == 0U)
  {
    if (tagSpiHasTransferError(spi) || (timeout-- == 0U))
    {
      tagSpiRecover(spi);
      return false;
    }
  }
  return true;
}

#if defined(SPI_TXDR_TXDR)
static bool tagSpiWaitClearCr1(SPI_TypeDef *spi, uint32_t mask)
{
  uint32_t timeout = TAG_SPI_POLL_LIMIT;

  while ((spi->CR1 & mask) != 0U)
  {
    if (tagSpiHasTransferError(spi) || (timeout-- == 0U))
    {
      tagSpiRecover(spi);
      return false;
    }
  }
  return true;
}
#endif

#if defined(SPI_TXDR_TXDR)
static bool tagSpiExchangeByte(SPI_TypeDef *spi, uint8_t tx, uint8_t *rx)
{
  volatile uint8_t *txdr = (volatile uint8_t *)&spi->TXDR;
  volatile uint8_t *rxdr = (volatile uint8_t *)&spi->RXDR;

  spi->CR1 |= SPI_CR1_CSTART;
  if (!tagSpiWaitSet(spi, SPI_SR_TXP))
    return false;
  *txdr = tx;
  if (!tagSpiWaitSet(spi, SPI_SR_RXP))
    return false;
  *rx = *rxdr;
  spi->CR1 |= SPI_CR1_CSUSP;
  if (!tagSpiWaitClearCr1(spi, SPI_CR1_CSTART))
    return false;
  spi->IFCR = 0xFFFFFFFFU;

  return true;
}
#else
static bool tagSpiExchangeByte(SPI_TypeDef *spi, uint8_t tx, uint8_t *rx)
{
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;

  if (!tagSpiWaitSet(spi, SPI_SR_TXE))
    return false;
  *spidr = tx;
  if (!tagSpiWaitSet(spi, SPI_SR_RXNE))
    return false;
  *rx = *spidr;
  return true;
}
#endif

static void tagSpiFillReadFailure(uint8_t *buf, uint32_t len)
{
  while (len--)
    *buf++ = TAG_SPI_ERROR_BYTE;
}

/**
 * @brief Pipelined SPI write for devices proven safe with queued transfers.
 *
 * @param[in] device SPI device descriptor.
 * @param[in] buf Bytes to transmit.
 * @param[in] len Number of bytes to transmit.
 */
bool tagSpiWritePipelined(const TagSpiDevice *device, const uint8_t *buf,
                          uint32_t len)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
#if defined(SPI_TXDR_TXDR)
  uint8_t rx;

  while (len--)
  {
    if (!tagSpiExchangeByte(spi, *buf++, &rx))
      return false;
  }
  return true;
#else
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;
  uint32_t read_len = len;
  uint32_t timeout = TAG_SPI_POLL_LIMIT;

  while (len || read_len) {
    bool progress = false;

    while (len && (spi->SR & SPI_SR_TXE)) {
      *spidr = *buf++;
      len--;
      progress = true;
    }
    while (read_len && (spi->SR & SPI_SR_RXNE)) {
      (void)*spidr;
      read_len--;
      progress = true;
    }
    if (tagSpiHasTransferError(spi)) {
      tagSpiRecover(spi);
      return false;
    }
    if (progress) {
      timeout = TAG_SPI_POLL_LIMIT;
    } else if (timeout-- == 0U) {
      tagSpiRecover(spi);
      return false;
    }
  }
  return true;
#endif
}

/**
 * @brief Pipelined SPI read for devices proven safe with queued transfers.
 *
 * @param[in] device SPI device descriptor.
 * @param[out] buf Buffer that receives bytes from the device.
 * @param[in] len Number of bytes to read.
 */
bool tagSpiReadPipelined(const TagSpiDevice *device, uint8_t *buf,
                         uint32_t len)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
#if defined(SPI_TXDR_TXDR)
  while (len--)
  {
    if (!tagSpiExchangeByte(spi, device->dummy, buf))
    {
      tagSpiFillReadFailure(buf, len + 1U);
      return false;
    }
    buf++;
  }
  return true;
#else
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;
  uint32_t read_len = len;
  uint32_t timeout = TAG_SPI_POLL_LIMIT;

  while (len || read_len) {
    bool progress = false;

    while (len && (spi->SR & SPI_SR_TXE)) {
      *spidr = device->dummy;
      len--;
      progress = true;
    }
    while (read_len && (spi->SR & SPI_SR_RXNE)) {
      *buf++ = *spidr;
      read_len--;
      progress = true;
    }
    if (tagSpiHasTransferError(spi)) {
      tagSpiRecover(spi);
      tagSpiFillReadFailure(buf, read_len);
      return false;
    }
    if (progress) {
      timeout = TAG_SPI_POLL_LIMIT;
    } else if (timeout-- == 0U) {
      tagSpiRecover(spi);
      tagSpiFillReadFailure(buf, read_len);
      return false;
    }
  }
  return true;
#endif
}

/**
 * @brief Write bytes and discard the full-duplex response stream.
 *
 * @param[in] device SPI device descriptor.
 * @param[in] buf Bytes to transmit.
 * @param[in] len Number of bytes to transmit.
 */
bool tagSpiWrite(const TagSpiDevice *device, const uint8_t *buf, uint32_t len)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
  uint8_t rx;

  while (len--)
  {
    if (!tagSpiExchangeByte(spi, *buf++, &rx))
      return false;
  }

  return true;
}

/**
 * @brief Read bytes by clocking the descriptor's dummy byte.
 *
 * @param[in] device SPI device descriptor.
 * @param[out] buf Buffer that receives bytes from the device.
 * @param[in] len Number of bytes to read.
 */
bool tagSpiRead(const TagSpiDevice *device, uint8_t *buf, uint32_t len)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);

  while (len--)
  {
    if (!tagSpiExchangeByte(spi, device->dummy, buf))
    {
      tagSpiFillReadFailure(buf, len + 1U);
      return false;
    }
    buf++;
  }

  return true;
}

/**
 * @brief Assert the SPI device chip-select line.
 *
 * @param[in] device SPI device descriptor to select.
 */
void tagSpiSelect(const TagSpiDevice *device)
{
  palClearLine(device->cs);
}

/**
 * @brief Deassert the SPI device chip-select line.
 *
 * @param[in] device SPI device descriptor to deselect.
 */
void tagSpiDeselect(const TagSpiDevice *device)
{
  palSetLine(device->cs);
}
/** @} */
