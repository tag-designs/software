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

#ifndef TAG_SPI_DMA_POLL_LIMIT
#define TAG_SPI_DMA_POLL_LIMIT 100000U
#endif

#ifndef TAG_SPI_DMA_BYTE_POLL_LIMIT
#define TAG_SPI_DMA_BYTE_POLL_LIMIT 64U
#endif

#ifndef TAG_SPI_TRANSFER_STATUS
#define TAG_SPI_TRANSFER_STATUS 0
#endif

#if defined(STM32_DMA3_PRESENT) && defined(SPI_CFG1_RXDMAEN) && \
    defined(SPI_CFG1_TXDMAEN)
#define TAG_SPI_DMA3_READ_SUPPORTED 1
#else
#define TAG_SPI_DMA3_READ_SUPPORTED 0
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
#if 0
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

#endif
#endif
//const TagSpiConfig tagSpiDefaultConfig = TAGSPIDEFAULTCONFIG();

/**
 * @brief Configure and enable the SPI peripheral for one device session.
 *
 * @param[in] device SPI device descriptor supplying peripheral and config.
 */
static void tagSpiDeviceEnable(const TagSpiDevice *device)
{
  //const TagSpiConfig config = device->config;
  /*
  if (!config)
  {
    config = &tagSpiDefaultConfig;
  }
    */

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
  device->spi->CR2 = device->config.cr2;
  device->spi->CR1 = device->config.cr1;
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

  palSetLine(device->config.ssline);
  palSetLineMode(device->config.ssline, PAL_MODE_OUTPUT_PUSHPULL);
}

/**
 * @brief Remove device power and return SPI pins to analog low-leakage mode.
 *
 * @param[in] device SPI device descriptor to power down.
 */
void tagSpiDevicePowerOff(const TagSpiDevice *device)
{
  palSetLine(device->config.ssline);
  palSetLineMode(device->config.ssline, PAL_MODE_OUTPUT_PUSHPULL);
  palSetLineMode(device->sck, PAL_MODE_INPUT_PULLDOWN);
  palSetLineMode(device->mosi, PAL_MODE_INPUT_PULLDOWN);
  palSetLineMode(device->miso, PAL_MODE_INPUT_PULLDOWN);
  
  if (tagLineIsValid(device->pwr))
  {
    palSetLineMode(device->config.ssline, PAL_MODE_INPUT_ANALOG);
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

  palSetLine(device->config.ssline);
  palSetLineMode(device->config.ssline,  PAL_MODE_OUTPUT_PUSHPULL);
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
  palSetLine(device->config.ssline);
  palClearLine(device->mosi);
  palClearLine(device->sck);

  tagSpiDeviceDisable(device);

  palSetLineMode(device->config.ssline, PAL_MODE_OUTPUT_PUSHPULL);
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
    tagEnableStandbyPullup(device->config.ssline);
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
#if TAG_SPI_TRANSFER_STATUS || defined(SPI_TXDR_TXDR)
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
  spi->CFG1 &= ~(SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);

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
static void tagSpiClearDmaRequests(SPI_TypeDef *spi)
{
  uint32_t cr1 = spi->CR1;
  uint32_t cfg1 = spi->CFG1;

  spi->CR1 = cr1 & ~SPI_CR1_SPE;
  spi->CFG1 = cfg1 & ~(SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);
  spi->CR1 = cr1;
}

static bool tagSpiExchangeByte(SPI_TypeDef *spi, uint8_t tx, uint8_t *rx)
{
  volatile uint8_t *txdr = (volatile uint8_t *)&spi->TXDR;
  volatile uint8_t *rxdr = (volatile uint8_t *)&spi->RXDR;

  tagSpiClearDmaRequests(spi);

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

#if TAG_SPI_DMA3_READ_SUPPORTED
typedef struct {
  uint32_t rx_channel_mask;
  uint32_t tx_channel_mask;
  uint32_t rx_request;
  uint32_t tx_request;
  uint32_t priority;
  uint32_t irq_priority;
} TagSpiDma3Config;

static const stm32_dma3_channel_t *tag_spi_rx_dma3;
static const stm32_dma3_channel_t *tag_spi_tx_dma3;
static uint8_t tag_spi_dma3_dummy;

static bool tagSpiDma3Config(const TagSpiDevice *device,
                             TagSpiDma3Config *config)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);

#if defined(SPI1) && defined(STM32_SPI_SPI1_RX_DMA3_CHANNEL) && \
    defined(STM32_SPI_SPI1_TX_DMA3_CHANNEL) &&                  \
    defined(STM32_DMA3_REQ_SPI1_RX) && defined(STM32_DMA3_REQ_SPI1_TX)
  if (spi == SPI1) {
    config->rx_channel_mask = STM32_SPI_SPI1_RX_DMA3_CHANNEL;
    config->tx_channel_mask = STM32_SPI_SPI1_TX_DMA3_CHANNEL;
    config->rx_request = STM32_DMA3_REQ_SPI1_RX;
    config->tx_request = STM32_DMA3_REQ_SPI1_TX;
    config->priority = STM32_SPI_SPI1_DMA_PRIORITY;
#if defined(STM32_SPI_SPI1_IRQ_PRIORITY)
    config->irq_priority = STM32_SPI_SPI1_IRQ_PRIORITY;
#else
    config->irq_priority = 0U;
#endif
    return true;
  }
#endif

  return false;
}

static bool tagSpiDma3EnsureChannels(const TagSpiDma3Config *config)
{
  bool allocated_rx = false;

  if (tag_spi_rx_dma3 == NULL) {
    tag_spi_rx_dma3 = dma3ChannelAlloc(config->rx_channel_mask,
                                       config->irq_priority, NULL, NULL);
    if (tag_spi_rx_dma3 == NULL)
      return false;
    allocated_rx = true;
  }

  if (tag_spi_tx_dma3 == NULL) {
    tag_spi_tx_dma3 = dma3ChannelAlloc(config->tx_channel_mask,
                                       config->irq_priority, NULL, NULL);
    if (tag_spi_tx_dma3 == NULL) {
      if (allocated_rx) {
        dma3ChannelFree(tag_spi_rx_dma3);
        tag_spi_rx_dma3 = NULL;
      }
      return false;
    }
  }

  return true;
}

static bool tagSpiDma3WaitComplete(uint32_t units)
{
  uint32_t timeout = TAG_SPI_DMA_POLL_LIMIT +
                     (units * TAG_SPI_DMA_BYTE_POLL_LIMIT);

  while ((dma3ChannelGetTransactionSize(tag_spi_rx_dma3) != 0U) ||
         (dma3ChannelGetTransactionSize(tag_spi_tx_dma3) != 0U))
  {
    uint32_t flags = tag_spi_rx_dma3->channel->CSR |
                     tag_spi_tx_dma3->channel->CSR;

    if ((flags & STM32_DMA3_CSR_ERRORS) != 0U)
      return false;
    if (timeout-- == 0U)
      return false;
  }

  if (((tag_spi_rx_dma3->channel->CSR | tag_spi_tx_dma3->channel->CSR) &
       STM32_DMA3_CSR_ERRORS) != 0U)
    return false;

  return true;
}

static bool tagSpiDma3Suspend(SPI_TypeDef *spi)
{
  uint32_t timeout = TAG_SPI_POLL_LIMIT;

  spi->CR1 |= SPI_CR1_CSUSP;
  while ((spi->CR1 & SPI_CR1_CSTART) != 0U)
  {
    if (tagSpiHasTransferError(spi))
      return false;
    if (timeout-- == 0U)
      return false;
  }
  spi->IFCR = 0xFFFFFFFFU;

  return true;
}

static void tagSpiDma3Stop(SPI_TypeDef *spi)
{
  uint32_t cr1 = spi->CR1;

  spi->CR1 = cr1 & ~SPI_CR1_SPE;
  spi->CFG1 &= ~(SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);
  spi->CR1 = cr1;
  dma3ChannelDisable(tag_spi_rx_dma3);
  dma3ChannelDisable(tag_spi_tx_dma3);
  spi->IFCR = 0xFFFFFFFFU;
}
#endif
#endif

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
#if !TAG_SPI_TRANSFER_STATUS && !defined(SPI_TXDR_TXDR)
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
  return true;
#elif defined(SPI_TXDR_TXDR)
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
#if !TAG_SPI_TRANSFER_STATUS && !defined(SPI_TXDR_TXDR)
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;
  uint32_t read_len = len;

  while (len || read_len) {
    while (len && (spi->SR & SPI_SR_TXE)) {
      *spidr = device->dummy;
      len--;
    }
    while (read_len && (spi->SR & SPI_SR_RXNE)) {
      *buf++ = *spidr;
      read_len--;
    }
  }
  return true;
#elif defined(SPI_TXDR_TXDR)
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
#if !TAG_SPI_TRANSFER_STATUS && !defined(SPI_TXDR_TXDR)
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;

  while (len--)
  {
    *spidr = *buf++;
    while ((spi->SR & SPI_SR_RXNE) == 0)
    {
      ;
    }
    (void)*spidr;
  }

  return true;
#else
  uint8_t rx;

  while (len--)
  {
    if (!tagSpiExchangeByte(spi, *buf++, &rx))
      return false;
  }

  return true;
#endif
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
#if !TAG_SPI_TRANSFER_STATUS && !defined(SPI_TXDR_TXDR)
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;

  while (len--)
  {
    *spidr = device->dummy;
    while ((spi->SR & SPI_SR_RXNE) == 0)
    {
      ;
    }
    *buf++ = *spidr;
  }

  return true;
#else

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
#endif
}

bool tagSpiReadDma(const TagSpiDevice *device, uint8_t *buf, uint32_t len)
{
#if TAG_SPI_DMA3_READ_SUPPORTED
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
  TagSpiDma3Config config;
  uint32_t cr_common;
  uint32_t rx_tr1;
  uint32_t tx_tr1;
  bool ok;

  if (len == 0U)
    return true;
  if (buf == NULL)
    return false;
  if (len > STM32_DMA3_MAX_TRANSFER)
    return false;
  if (!tagSpiDma3Config(device, &config))
    return false;
  if (!tagSpiDma3EnsureChannels(&config))
    return false;

  tagSpiRecover(spi);
  tag_spi_dma3_dummy = device->dummy;

  cr_common = STM32_DMA3_CCR_PRIO(config.priority) |
              STM32_DMA3_CCR_LAP_MEM;

  rx_tr1 = STM32_DMA3_CTR1_SAP_PER |
           STM32_DMA3_CTR1_DAP_MEM |
           STM32_DMA3_CTR1_SDW_BYTE |
           STM32_DMA3_CTR1_DDW_BYTE |
           STM32_DMA3_CTR1_DINC;

  tx_tr1 = STM32_DMA3_CTR1_SAP_MEM |
           STM32_DMA3_CTR1_DAP_PER |
           STM32_DMA3_CTR1_SDW_BYTE |
           STM32_DMA3_CTR1_DDW_BYTE;

  tag_spi_rx_dma3->channel->CFCR = STM32_DMA3_CFCR_ALL_FLAGS;
  tag_spi_tx_dma3->channel->CFCR = STM32_DMA3_CFCR_ALL_FLAGS;

  dma3ChannelSetSource(tag_spi_rx_dma3, &spi->RXDR);
  dma3ChannelSetDestination(tag_spi_rx_dma3, buf);
  dma3ChannelSetTransactionSize(tag_spi_rx_dma3, len);
  dma3ChannelSetMode(tag_spi_rx_dma3,
                     cr_common,
                     rx_tr1,
                     STM32_DMA3_CTR2_REQSEL(config.rx_request),
                     0U);

  dma3ChannelSetSource(tag_spi_tx_dma3, &tag_spi_dma3_dummy);
  dma3ChannelSetDestination(tag_spi_tx_dma3, &spi->TXDR);
  dma3ChannelSetTransactionSize(tag_spi_tx_dma3, len);
  dma3ChannelSetMode(tag_spi_tx_dma3,
                     cr_common,
                     tx_tr1,
                     STM32_DMA3_CTR2_REQSEL(config.tx_request) |
                         STM32_DMA3_CTR2_DREQ,
                     0U);

  spi->CFG1 |= SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN;
  dma3ChannelEnable(tag_spi_rx_dma3);
  dma3ChannelEnable(tag_spi_tx_dma3);
  spi->CR1 |= SPI_CR1_CSTART;

  ok = tagSpiDma3WaitComplete(len);
  if (ok)
    ok = tagSpiDma3Suspend(spi);

  tagSpiDma3Stop(spi);

  if (!ok) {
    tagSpiRecover(spi);
    return false;
  }

  return true;
#else
  (void)device;
  (void)buf;
  (void)len;
  return false;
#endif
}

/**
 * @brief Assert the SPI device chip-select line.
 *
 * @param[in] device SPI device descriptor to select.
 */
void tagSpiSelect(const TagSpiDevice *device)
{
  palClearLine(device->config.ssline);
}

/**
 * @brief Deassert the SPI device chip-select line.
 *
 * @param[in] device SPI device descriptor to deselect.
 */
void tagSpiDeselect(const TagSpiDevice *device)
{
  palSetLine(device->config.ssline);
}
/** @} */
