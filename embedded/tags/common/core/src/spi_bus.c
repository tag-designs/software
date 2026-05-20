#include "power.h"

#include "core_sync.h"
#include "gpio_utils.h"

/*
 * Polling SPI byte transfers shared by simple peripheral drivers.
 *
 * Existing tag code drives SPI controller registers directly rather than
 * through ChibiOS SPI transactions, so these helpers preserve that behavior
 * while centralizing the repeated full-duplex drain/read loops. This file also
 * owns SPI bus-session pin state, shared controller configuration,
 * active-state tracking, and bus mutex for descriptor-backed SPI devices.
 *
 * Storage flash drivers can be more sensitive to command/address/data pacing.
 * They should use storage_spi.h when they need byte-at-a-time transfers with
 * chip select held across a complete flash transaction.
 */

static bool spi1_on = false;
static bool spi1_suspended_for_stop = false;

bool isSpi1On(void)
{
  return spi1_on;
}

void tagMarkSpi1On(void)
{
#if defined(STM32_HAS_SPI1) && STM32_HAS_SPI1
  spi1_on = true;
#endif
}

void tagMarkSpi1Off(void)
{
#if defined(STM32_HAS_SPI1) && STM32_HAS_SPI1
  spi1_on = false;
#endif
}

void tagSpiDisableActiveForStop(void)
{
#if defined(STM32_HAS_SPI1) && STM32_HAS_SPI1
  if (spi1_on)
  {
    SPI1->CR1 &= ~SPI_CR1_SPE;
    spi1_suspended_for_stop = true;
  }
#endif
}

void tagSpiEnableActiveAfterStop(void)
{
#if defined(STM32_HAS_SPI1) && STM32_HAS_SPI1
  if (spi1_suspended_for_stop)
  {
    SPI1->CR1 |= SPI_CR1_SPE;
    spi1_suspended_for_stop = false;
  }
#endif
}

#if defined(STM32_HAS_SPI1) && STM32_HAS_SPI1
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

const TagSpiController tagSpi1DefaultController = {
    .spi = SPI1,
    .mutex = &SPImutex,
};

void tagSpiControllerEnable(const TagSpiController *controller,
                            const TagSpiConfig *config)
{
#if defined(STM32_HAS_SPI1) && STM32_HAS_SPI1
  if (!config)
  {
    config = &tagSpiDefaultConfig;
  }

  if (controller->spi == SPI1)
  {
    rccEnableSPI1(0);
    rccResetSPI1();
  }

  controller->spi->CR1 = 0;
  controller->spi->CR2 = config->cr2;
  controller->spi->CR1 = config->cr1;
  controller->spi->CR1 |= SPI_CR1_SPE;

  if (controller->spi == SPI1)
  {
    tagMarkSpi1On();
  }
#else
  (void)controller;
  (void)config;
#endif
}

void tagSpiControllerDisable(const TagSpiController *controller)
{
#if defined(STM32_HAS_SPI1) && STM32_HAS_SPI1
  controller->spi->CR1 = 0;
  controller->spi->CR2 = 0;

  if (controller->spi == SPI1)
  {
    tagMarkSpi1Off();
  }
#else
  (void)controller;
#endif
}

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
  const TagSpiController *controller = device->controller;

  if (controller && controller->mutex)
  {
    chBSemWait(controller->mutex);
  }

  palSetLine(device->cs);
  toOutput(device->cs);

  toAlternate(device->sck);
  toAlternate(device->miso);
  toAlternate(device->mosi);

  if (controller)
  {
    tagSpiControllerEnable(controller, device->config);
  }
}

void tagSpiBusEnd(const TagSpiDevice *device)
{
  const TagSpiController *controller = device->controller;

  palSetLine(device->cs);

  if (controller)
  {
    tagSpiControllerDisable(controller);
  }

  toAnalog(device->sck);
  toAnalog(device->mosi);
  toAnalog(device->miso);

  if (controller && controller->mutex)
  {
    chBSemSignal(controller->mutex);
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
