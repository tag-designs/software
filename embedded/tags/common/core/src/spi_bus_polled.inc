/**
 * @file spi_bus_polled.inc
 * @brief Polled SPI register initialization and raw byte transfers.
 */

#ifndef TAG_SPI_POLL_LIMIT
#define TAG_SPI_POLL_LIMIT 100000U
#endif

static void tagSpiPeripheralEnableClock(SPI_TypeDef *spi)
{
#if defined(STM32_SPI_USE_SPI1) && STM32_SPI_USE_SPI1
  if (spi == SPI1) {
    rccEnableSPI1(0);
    rccResetSPI1();
    return;
  }
#endif
#if defined(STM32_SPI_USE_SPI2) && STM32_SPI_USE_SPI2
  if (spi == SPI2) {
    rccEnableSPI2(0);
    rccResetSPI2();
    return;
  }
#endif
#if defined(STM32_SPI_USE_SPI3) && STM32_SPI_USE_SPI3
  if (spi == SPI3) {
    rccEnableSPI3(0);
    rccResetSPI3();
    return;
  }
#endif
#if defined(STM32_SPI_USE_SPI4) && STM32_SPI_USE_SPI4
  if (spi == SPI4) {
    rccEnableSPI4(0);
    rccResetSPI4();
    return;
  }
#endif
#if defined(STM32_SPI_USE_SPI5) && STM32_SPI_USE_SPI5
  if (spi == SPI5) {
    rccEnableSPI5(0);
    rccResetSPI5();
    return;
  }
#endif
#if defined(STM32_SPI_USE_SPI6) && STM32_SPI_USE_SPI6
  if (spi == SPI6) {
    rccEnableSPI6(0);
    rccResetSPI6();
    return;
  }
#endif
  (void)spi;
}

static void tagSpiDeviceEnable(const TagSpiDevice *device)
{
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

#if !TAG_SPI_TRANSFER_STATUS && !defined(SPI_TXDR_TXDR)
static bool tagSpiExchangeByte(SPI_TypeDef *spi, uint8_t tx, uint8_t *rx)
{
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;

  *spidr = tx;
  while ((spi->SR & SPI_SR_RXNE) == 0) {
    ;
  }
  *rx = *spidr;
  return true;
}
#else
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

  while ((spi->SR & mask) == 0U) {
    if (tagSpiHasTransferError(spi) || (timeout-- == 0U)) {
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

  while ((spi->CR1 & mask) != 0U) {
    if (tagSpiHasTransferError(spi) || (timeout-- == 0U)) {
      tagSpiRecover(spi);
      return false;
    }
  }
  return true;
}

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
#endif

bool tagSpiWrite(const TagSpiDevice *device, const uint8_t *buf, uint32_t len)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
#if !TAG_SPI_TRANSFER_STATUS && !defined(SPI_TXDR_TXDR)
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;

  while (len--) {
    *spidr = *buf++;
    while ((spi->SR & SPI_SR_RXNE) == 0) {
      ;
    }
    (void)*spidr;
  }

  return true;
#else
  uint8_t rx;

  while (len--) {
    if (!tagSpiExchangeByte(spi, *buf++, &rx))
      return false;
  }

  return true;
#endif
}

bool tagSpiRead(const TagSpiDevice *device, uint8_t *buf, uint32_t len)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
#if !TAG_SPI_TRANSFER_STATUS && !defined(SPI_TXDR_TXDR)
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;

  while (len--) {
    *spidr = device->dummy;
    while ((spi->SR & SPI_SR_RXNE) == 0) {
      ;
    }
    *buf++ = *spidr;
  }

  return true;
#else
  while (len--) {
    if (!tagSpiExchangeByte(spi, device->dummy, buf)) {
      tagSpiFillReadFailure(buf, len + 1U);
      return false;
    }
    buf++;
  }

  return true;
#endif
}

bool tagSpiExchange(const TagSpiDevice *device, const uint8_t *txbuf,
                    uint8_t *rxbuf, uint32_t len)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);

  while (len--) {
    if (!tagSpiExchangeByte(spi, *txbuf++, rxbuf)) {
      tagSpiFillReadFailure(rxbuf, len + 1U);
      return false;
    }
    rxbuf++;
  }

  return true;
}

void tagSpiSelect(const TagSpiDevice *device)
{
  palClearLine(device->config.ssline);
}

void tagSpiDeselect(const TagSpiDevice *device)
{
  palSetLine(device->config.ssline);
}
