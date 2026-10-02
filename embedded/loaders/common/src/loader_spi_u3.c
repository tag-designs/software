/**
 * @file    loader_spi_u3.c
 * @brief   Polled, bounded SPI master for the STM32U3 SPI peripheral (SPIv4:
 *          SPI_TXDR/SPI_RXDR, CSTART/CSUSP).
 *
 * @details The STM32U375 counterpart of loader_spi.c, implementing the same
 *          loader.h interface. The register sequence follows the tag
 *          firmware's polled path, common/core/src/spi_bus_polled.inc:
 *          - configure with CR1 cleared: CR2 = 0 (TSIZE 0, an endless
 *            transfer), IER = 0, every flag cleared, CFG1 = 8-bit frames with
 *            MBR = /2, CFG2 = MASTER | SSOE (mode 0, MSB first), then
 *            CR1 = MASRX | SPE;
 *          - per byte: CSTART, wait TXP and write TXDR, wait RXP and read RXDR,
 *            then CSUSP and wait for CSTART to clear, and clear the flags.
 *          Suspending after every byte keeps the transfer idle between bytes,
 *          so chip select can be released at any point.
 *
 *          The kernel clock is left at its reset selection, PCLK2
 *          (RCC_CCIPR1.SPI1SEL = 0), which at the 12 MHz pinned by
 *          loader_clock_u3.c gives SCK = 6 MHz.
 *
 *          Every wait is bounded by LOADER_SPI_POLL_LIMIT and fails on overrun
 *          or mode fault; the caller then releases chip select.
 */

#include "loader.h"

/** @brief Upper bound on status polls per wait. */
#define LOADER_SPI_POLL_LIMIT 10000U

/** @brief Status bits that end any wait as a failure. */
#define LOADER_SPI_ERROR_BITS (SPI_SR_OVR | SPI_SR_MODF)

/** @brief Enable and reset the SPI instance @p spi (SPI1, SPI2 or SPI3). */
static void loaderSpiEnableClock(SPI_TypeDef *spi)
{
  if (spi == SPI1) {
    rccEnableSPI1(false);
    rccResetSPI1();
  }
#if defined(SPI2)
  else if (spi == SPI2) {
    rccEnableSPI2(false);
    rccResetSPI2();
  }
#endif
#if defined(SPI3)
  else if (spi == SPI3) {
    rccEnableSPI3(false);
    rccResetSPI3();
  }
#endif
}

/** @brief Wait for all of @p mask in SR, failing on an error bit. */
static bool loaderSpiWaitSet(SPI_TypeDef *spi, uint32_t mask)
{
  uint32_t n;

  for (n = 0; n < LOADER_SPI_POLL_LIMIT; n++) {
    const uint32_t sr = spi->SR;
    if ((sr & LOADER_SPI_ERROR_BITS) != 0U)
      return false;
    if ((sr & mask) == mask)
      return true;
  }
  return false;
}

/** @brief Wait for @p mask to clear in CR1. */
static bool loaderSpiWaitClearCr1(SPI_TypeDef *spi, uint32_t mask)
{
  uint32_t n;

  for (n = 0; n < LOADER_SPI_POLL_LIMIT; n++) {
    if ((spi->CR1 & mask) == 0U)
      return true;
  }
  return false;
}

/* Contract documented in loader.h. */
void loaderSpiInit(const LoaderSpiBus *bus)
{
  SPI_TypeDef *spi = bus->spi;

  palSetLine(bus->cs);
  palSetLineMode(bus->cs, PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_MID2);
  palSetLineMode(bus->sck, PAL_MODE_ALTERNATE(bus->alternate_function) |
                               PAL_STM32_OSPEED_MID2);
  palSetLineMode(bus->miso, PAL_MODE_ALTERNATE(bus->alternate_function) |
                                PAL_STM32_OSPEED_MID2);
  palSetLineMode(bus->mosi, PAL_MODE_ALTERNATE(bus->alternate_function) |
                                PAL_STM32_OSPEED_MID2);

  loaderSpiEnableClock(spi);
  spi->CR1 = 0;
  spi->CR2 = 0;
  spi->IER = 0;
  spi->IFCR = 0xFFFFFFFFU;
  spi->CFG1 = (7U << SPI_CFG1_DSIZE_Pos);       /* 8-bit, MBR = /2 */
  spi->CFG2 = SPI_CFG2_MASTER | SPI_CFG2_SSOE;  /* mode 0, MSB first */
  spi->CR1 = SPI_CR1_MASRX | SPI_CR1_SPE;
}

/* Contract documented in loader.h. */
void loaderSpiSelect(const LoaderSpiBus *bus)
{
  palClearLine(bus->cs);
}

/* Contract documented in loader.h. */
void loaderSpiDeselect(const LoaderSpiBus *bus)
{
  /* Each byte ends suspended (CSUSP), so the bus is already idle. */
  palSetLine(bus->cs);
}

/* Contract documented in loader.h. */
bool loaderSpiExchange(const LoaderSpiBus *bus, uint8_t tx, uint8_t *rx)
{
  SPI_TypeDef *spi = bus->spi;
  uint8_t value;
  bool ok = false;

  spi->CR1 |= SPI_CR1_CSTART;
  do {
    if (!loaderSpiWaitSet(spi, SPI_SR_TXP))
      break;
    *(volatile uint8_t *)&spi->TXDR = tx;
    if (!loaderSpiWaitSet(spi, SPI_SR_RXP))
      break;
    value = *(volatile uint8_t *)&spi->RXDR;
    if (rx != NULL)
      *rx = value;
    ok = true;
  } while (0);

  spi->CR1 |= SPI_CR1_CSUSP;
  if (!loaderSpiWaitClearCr1(spi, SPI_CR1_CSTART))
    ok = false;
  spi->IFCR = 0xFFFFFFFFU;
  return ok;
}

/* Contract documented in loader.h. */
bool loaderSpiSend(const LoaderSpiBus *bus, const uint8_t *buf, uint32_t n)
{
  while (n-- > 0U) {
    if (!loaderSpiExchange(bus, *buf++, NULL))
      return false;
  }
  return true;
}

/* Contract documented in loader.h. */
bool loaderSpiReceive(const LoaderSpiBus *bus, uint8_t *buf, uint32_t n)
{
  while (n-- > 0U) {
    if (!loaderSpiExchange(bus, 0xFFU, buf++))
      return false;
  }
  return true;
}
