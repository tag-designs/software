#include "power.h"

#include "core_sync.h"
#include "gpio_utils.h"

/*
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

/*
 * Active-peripheral tracking for Stop2.
 *
 * Short sleeps suspend active peripherals without changing device power,
 * chip-select ownership, or pin alternate-function setup.
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

static void tagSpiEnableAfterStop(SPI_TypeDef *spi)
{
  bool *suspended = tagSpiSuspendedForStopFlag(spi);

  if (suspended && *suspended)
  {
    spi->CR1 |= SPI_CR1_SPE;
    *suspended = false;
  }
}

bool tagSpiIsOn(SPI_TypeDef *spi)
{
  bool *on = tagSpiOnFlag(spi);

  return on && *on;
}

void tagMarkSpiOn(SPI_TypeDef *spi)
{
  bool *on = tagSpiOnFlag(spi);

  if (on)
  {
    *on = true;
  }
}

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

#if (defined(STM32_SPI_USE_SPI1) && STM32_SPI_USE_SPI1) ||                         \
    (defined(STM32_SPI_USE_SPI2) && STM32_SPI_USE_SPI2) ||                         \
    (defined(STM32_SPI_USE_SPI3) && STM32_SPI_USE_SPI3) ||                         \
    (defined(STM32_SPI_USE_SPI4) && STM32_SPI_USE_SPI4) ||                         \
    (defined(STM32_SPI_USE_SPI5) && STM32_SPI_USE_SPI5) ||                         \
    (defined(STM32_SPI_USE_SPI6) && STM32_SPI_USE_SPI6)
const TagSpiConfig tagSpiDefaultConfig = {
    .cr1 = SPI_CR1_MSTR,
    .cr2 = SPI_CR2_FRXTH | SPI_CR2_SSOE | SPI_CR2_DS_2 |
           SPI_CR2_DS_1 | SPI_CR2_DS_0,
};

#else
const TagSpiConfig tagSpiDefaultConfig = {
    .cr1 = 0,
    .cr2 = 0,
};
#endif

static void tagSpiDeviceEnable(const TagSpiDevice *device)
{
  const TagSpiConfig *config = device->config;
  if (!config)
  {
    config = &tagSpiDefaultConfig;
  }

  tagSpiPeripheralEnableClock(device->spi);

  device->spi->CR1 = 0;
  device->spi->CR2 = config->cr2;
  device->spi->CR1 = config->cr1;
  device->spi->CR1 |= SPI_CR1_SPE;

  tagMarkSpiOn(device->spi);
}

static void tagSpiDeviceDisable(const TagSpiDevice *device)
{
  device->spi->CR1 = 0;
  device->spi->CR2 = 0;

  tagMarkSpiOff(device->spi);
}

/*
 * Device power and bus sessions.
 *
 * Power on/off handles optional switched power and safe idle pins. Bus
 * begin/end owns the shared mutex, alternate functions, and peripheral enable
 * state for one transaction/session.
 */
void tagSpiDevicePowerOn(const TagSpiDevice *device)
{
  if (tagLineIsValid(device->pwr))
  {
    toOutput(device->pwr);
    palSetLine(device->pwr);
  }

  palSetLine(device->cs);
  toOutput(device->cs);
}

void tagSpiDevicePowerOff(const TagSpiDevice *device)
{
  if (tagLineIsValid(device->pwr))
  {
    palClearLine(device->pwr);
  }

  toAnalog(device->sck);
  toAnalog(device->mosi);
  toAnalog(device->miso);
  toAnalog(device->cs);
}

void tagSpiBusBegin(const TagSpiDevice *device)
{
  if (device->mutex)
  {
    chBSemWait(device->mutex);
  }

  palSetLine(device->cs);
  toOutput(device->cs);

  toAlternate(device->sck);
  toAlternate(device->miso);
  toAlternate(device->mosi);

  tagSpiDeviceEnable(device);
}

void tagSpiBusEnd(const TagSpiDevice *device)
{
  palSetLine(device->cs);

  tagSpiDeviceDisable(device);

  toAnalog(device->sck);
  toAnalog(device->mosi);
  toAnalog(device->miso);

  if (device->mutex)
  {
    chBSemSignal(device->mutex);
  }
}

void tagSpiDevicePrepareSleep(const TagSpiDevice *device)
{
  switch (device->sleep_policy)
  {
  case TAG_SPI_SLEEP_SAFE_IDLE:
    tagEnableStandbyPullup(device->cs);
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

/*
 * Raw byte transfers.
 *
 * The default read/write routines intentionally use byte-at-a-time
 * request/response loops. Pipelined variants remain available for cases that
 * have been proven safe on hardware.
 */
void tagSpiWritePipelined(const TagSpiDevice *device, const uint8_t *buf,
                          uint32_t len)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
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

void tagSpiReadPipelined(const TagSpiDevice *device, uint8_t *buf,
                         uint32_t len)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
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
}

void tagSpiWrite(const TagSpiDevice *device, const uint8_t *buf, uint32_t len)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
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
}

void tagSpiRead(const TagSpiDevice *device, uint8_t *buf, uint32_t len)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
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
}

void tagSpiSelect(const TagSpiDevice *device)
{
  palClearLine(device->cs);
}

void tagSpiDeselect(const TagSpiDevice *device)
{
  palSetLine(device->cs);
}
