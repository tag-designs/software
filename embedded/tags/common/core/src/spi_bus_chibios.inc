/**
 * @file spi_bus_chibios.inc
 * @brief ChibiOS HAL SPI-backed initialization and transfers.
 */

#if !defined(HAL_LLD_SELECT_SPI_V2) || (HAL_LLD_SELECT_SPI_V2 != TRUE)
#error "The ChibiOS SPI backend requires the ChibiOS SPI v2 driver model"
#endif

#if !defined(SPI_USE_SYNCHRONIZATION) || (SPI_USE_SYNCHRONIZATION != TRUE)
#error "The ChibiOS SPI backend requires SPI_USE_WAIT/SPI_USE_SYNCHRONIZATION"
#endif

static SPIDriver *tagSpiDriver(SPI_TypeDef *spi)
{
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
  (void)spi;
  return NULL;
}

static SPIDriver *tagSpiDeviceDriver(const TagSpiDevice *device)
{
  return tagSpiDriver(tagSpiDevicePeripheral(device));
}

static void tagSpiDeviceEnable(const TagSpiDevice *device)
{
  SPIDriver *driver = tagSpiDeviceDriver(device);

  if (driver == NULL)
    return;

  (void)spiStart(driver, &device->config.spi);
  tagMarkSpiOn(tagSpiDevicePeripheral(device));
}

static void tagSpiDeviceDisable(const TagSpiDevice *device)
{
  SPIDriver *driver = tagSpiDeviceDriver(device);

  if (driver != NULL)
    spiStop(driver);
  tagMarkSpiOff(tagSpiDevicePeripheral(device));
}

bool tagSpiWrite(const TagSpiDevice *device, const uint8_t *buf, uint32_t len)
{
  SPIDriver *driver = tagSpiDeviceDriver(device);

  if (driver == NULL)
    return false;
  return spiSend(driver, len, buf) == MSG_OK;
}

bool tagSpiRead(const TagSpiDevice *device, uint8_t *buf, uint32_t len)
{
  SPIDriver *driver = tagSpiDeviceDriver(device);

  if (driver == NULL) {
    tagSpiFillReadFailure(buf, len);
    return false;
  }
  if (spiReceive(driver, len, buf) != MSG_OK) {
    tagSpiFillReadFailure(buf, len);
    return false;
  }
  return true;
}

bool tagSpiExchange(const TagSpiDevice *device, const uint8_t *txbuf,
                    uint8_t *rxbuf, uint32_t len)
{
  SPIDriver *driver = tagSpiDeviceDriver(device);

  if (driver == NULL) {
    tagSpiFillReadFailure(rxbuf, len);
    return false;
  }
  if (spiExchange(driver, len, txbuf, rxbuf) != MSG_OK) {
    tagSpiFillReadFailure(rxbuf, len);
    return false;
  }
  return true;
}

void tagSpiSelect(const TagSpiDevice *device)
{
  SPIDriver *driver = tagSpiDeviceDriver(device);

  if (driver != NULL && driver->config != NULL)
    spiSelect(driver);
  else
    palClearLine(device->config.ssline);
}

void tagSpiDeselect(const TagSpiDevice *device)
{
  SPIDriver *driver = tagSpiDeviceDriver(device);

  if (driver != NULL && driver->config != NULL)
    spiUnselect(driver);
  else
    palSetLine(device->config.ssline);
}
