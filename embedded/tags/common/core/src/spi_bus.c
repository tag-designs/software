#include "spi_bus.h"

#include "core_sync.h"

/*
 * Polling SPI byte transfers shared by simple peripheral drivers.
 *
 * Existing tag code drives SPI controller registers directly rather than
 * through ChibiOS SPI transactions, so these helpers preserve that behavior
 * while centralizing the repeated full-duplex drain/read loops. This file also
 * owns the shared SPI controller configuration, active-state tracking, and bus
 * mutex for descriptor-backed SPI devices.
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

static void spi1DefaultEnable(const TagSpiConfig *config)
{
  if (!config)
  {
    config = &tagSpiDefaultConfig;
  }

  rccEnableSPI1(0);
  rccResetSPI1();

  SPI1->CR1 = 0;
  SPI1->CR2 = config->cr2;
  SPI1->CR1 = config->cr1;
  SPI1->CR1 |= SPI_CR1_SPE;
  tagMarkSpi1On();
}

static void spi1DefaultDisable(void)
{
  SPI1->CR1 = 0;
  SPI1->CR2 = 0;
  tagMarkSpi1Off();
}
#else
const TagSpiConfig tagSpiDefaultConfig = {
    .cr1 = 0,
    .cr2 = 0,
};

static void spi1DefaultEnable(const TagSpiConfig *config)
{
  (void)config;
}

static void spi1DefaultDisable(void)
{
}
#endif

const TagSpiController tagSpi1DefaultController = {
    .mutex = &SPImutex,
    .enable = spi1DefaultEnable,
    .disable = spi1DefaultDisable,
};

void tagSpiWrite(SPI_TypeDef *spi, const uint8_t *buf, uint32_t len)
{
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

void tagSpiRead(SPI_TypeDef *spi, uint8_t *buf, uint32_t len)
{
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;
  uint32_t read_len = len;

  while (len || read_len) {
    while (len && (spi->SR & SPI_SR_TXE)) {
      *spidr = 0xff;
      len--;
    }
    while (read_len && (spi->SR & SPI_SR_RXNE)) {
      *buf++ = *spidr;
      read_len--;
    }
  }
}

void tagSpiSelect(const TagSpiBus *bus)
{
  palClearLine(bus->cs);
}

void tagSpiDeselect(const TagSpiBus *bus)
{
  palSetLine(bus->cs);
}

void tagSpiBusWrite(const TagSpiBus *bus, const uint8_t *buf, uint32_t len)
{
  tagSpiSelect(bus);
  tagSpiWrite(bus->spi, buf, len);
  tagSpiDeselect(bus);
}

void tagSpiBusRead(const TagSpiBus *bus, uint8_t *buf, uint32_t len)
{
  tagSpiSelect(bus);
  tagSpiRead(bus->spi, buf, len);
  tagSpiDeselect(bus);
}
