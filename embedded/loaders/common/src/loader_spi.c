/**
 * @file    loader_spi.c
 * @brief   Polled, bounded SPI master for external loaders (STM32 SPIv2).
 *
 * @details Matches the tag firmware's polled configuration in
 *          tags/common/core/inc/spi_bus.h (TAG_SPI_POLLED_CONFIG_FIELDS):
 *          mode 0, 8-bit frames, clock = PCLK / 2, chip select driven as a
 *          GPIO. At the loader's 16 MHz PCLK that is 8 MHz on SCK.
 *
 *          Data-register accesses are byte-wide. On SPIv2 a 16-bit access to
 *          DR packs two frames, which is a silent source of shifted data.
 */

#include "loader.h"

/** @brief Iteration bound for one byte's TXE/RXNE/BSY waits. */
#define LOADER_SPI_POLL_LIMIT 10000U

/**
 * @brief   Enable and reset the SPI peripheral's clock.
 *
 * @param[in] spi   SPI1 or SPI3; any other instance is left alone and the
 *                  following transfers time out.
 */
static void loaderSpiEnableClock(SPI_TypeDef *spi)
{
  if (spi == SPI1) {
    rccEnableSPI1(false);
    rccResetSPI1();
  }
#if defined(SPI3)
  else if (spi == SPI3) {
    rccEnableSPI3(false);
    rccResetSPI3();
  }
#endif
}

/**
 * @brief   Wait for a status flag to reach a value, bounded.
 *
 * @return  true when (SR & mask) == value within LOADER_SPI_POLL_LIMIT reads.
 */
static bool loaderSpiWait(SPI_TypeDef *spi, uint32_t mask, uint32_t value)
{
  uint32_t n;

  for (n = 0; n < LOADER_SPI_POLL_LIMIT; n++) {
    if ((spi->SR & mask) == value)
      return true;
  }
  return false;
}

/* Contract documented in loader.h. */
void loaderSpiInit(const LoaderSpiBus *bus)
{
  palSetLine(bus->cs);
  palSetLineMode(bus->cs, PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_MID2);
  palSetLineMode(bus->sck, PAL_MODE_ALTERNATE(bus->alternate_function) |
                               PAL_STM32_OSPEED_MID2);
  palSetLineMode(bus->miso, PAL_MODE_ALTERNATE(bus->alternate_function) |
                                PAL_STM32_OSPEED_MID2);
  palSetLineMode(bus->mosi, PAL_MODE_ALTERNATE(bus->alternate_function) |
                                PAL_STM32_OSPEED_MID2);

  loaderSpiEnableClock(bus->spi);
  bus->spi->CR1 = 0;
  bus->spi->CR2 = SPI_CR2_FRXTH | SPI_CR2_SSOE | SPI_CR2_DS_2 | SPI_CR2_DS_1 |
                  SPI_CR2_DS_0;
  bus->spi->CR1 = SPI_CR1_MSTR;
  bus->spi->CR1 |= SPI_CR1_SPE;
}

/* Contract documented in loader.h. */
void loaderSpiSelect(const LoaderSpiBus *bus)
{
  palClearLine(bus->cs);
}

/* Contract documented in loader.h. */
void loaderSpiDeselect(const LoaderSpiBus *bus)
{
  (void)loaderSpiWait(bus->spi, SPI_SR_BSY, 0U);
  palSetLine(bus->cs);
}

/* Contract documented in loader.h. */
bool loaderSpiExchange(const LoaderSpiBus *bus, uint8_t tx, uint8_t *rx)
{
  volatile uint8_t *dr = (volatile uint8_t *)&bus->spi->DR;
  uint8_t value;

  if (!loaderSpiWait(bus->spi, SPI_SR_TXE, SPI_SR_TXE))
    return false;
  *dr = tx;
  if (!loaderSpiWait(bus->spi, SPI_SR_RXNE, SPI_SR_RXNE))
    return false;
  value = *dr;
  if (rx != NULL)
    *rx = value;
  return true;
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
