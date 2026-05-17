#include "power.h"

#include "gpio_utils.h"

/*
 * Shared bus and standby-pin helpers.
 *
 * Device modules describe their board-provided LINE_xxx pins with small
 * descriptors. These helpers provide the common STM32 mechanics for SPI/I2C
 * enable/disable sequencing and standby pull configuration; device modules
 * still own the descriptor and any device-specific delays or reset handling.
 */

bool tagLineIsValid(ioline_t line)
{
  return line != TAG_NO_LINE;
}

void tagEnableStandbyPullup(ioline_t line)
{
  if (!tagLineIsValid(line))
  {
    return;
  }

  if ((PAL_PAD(line) != 14) && (PAL_PORT(line) == GPIOA))
  {
    SET_BIT(PWR->PUCRA, 1 << PAL_PAD(line));
  }
  if (PAL_PORT(line) == GPIOB)
  {
    SET_BIT(PWR->PUCRB, 1 << PAL_PAD(line));
  }
}

void tagEnableStandbyPulldown(ioline_t line)
{
  if (!tagLineIsValid(line))
  {
    return;
  }

  if ((PAL_PAD(line) != 13) && (PAL_PAD(line) != 15) && (PAL_PORT(line) == GPIOA))
  {
    SET_BIT(PWR->PDCRA, 1 << PAL_PAD(line));
  }
  if (PAL_PORT(line) == GPIOB)
  {
    SET_BIT(PWR->PDCRB, 1 << PAL_PAD(line));
  }
}

static void spi1DefaultEnable(void)
{
  rccEnableSPI1(0);
  rccResetSPI1();

  SPI1->CR1 = 0;
  SPI1->CR2 = SPI_CR2_FRXTH | SPI_CR2_SSOE | SPI_CR2_DS_2 |
              SPI_CR2_DS_1 | SPI_CR2_DS_0;
  SPI1->CR1 = SPI_CR1_MSTR;
  SPI1->CR1 |= SPI_CR1_SPE;
}

static void spi1DefaultDisable(void)
{
  SPI1->CR1 = 0;
  SPI1->CR2 = 0;
}

const TagSpiController tagSpi1DefaultController = {
    .enable = spi1DefaultEnable,
    .disable = spi1DefaultDisable,
};

void tagSpiDeviceOn(const TagSpiDevice *device)
{
  if (device->mutex)
  {
    chBSemWait(device->mutex);
  }

  if (tagLineIsValid(device->pwr))
  {
    toOutput(device->pwr);
    palSetLine(device->pwr);
  }

  palSetLine(device->cs);
  toOutput(device->cs);

  toAlternate(device->sck);
  toAlternate(device->miso);
  toAlternate(device->mosi);

  if (device->controller && device->controller->enable)
  {
    device->controller->enable();
  }
}

void tagSpiDeviceOff(const TagSpiDevice *device)
{
  palSetLine(device->cs);

  if (device->controller && device->controller->disable)
  {
    device->controller->disable();
  }

  switch (device->off_policy)
  {
  case TAG_SPI_OFF_SAFE_IDLE:
    toOutput(device->cs);
    palClearLine(device->sck);
    toOutput(device->sck);
    palClearLine(device->mosi);
    toOutput(device->mosi);
    toInput(device->miso);
    break;

  case TAG_SPI_OFF_FLOAT:
    toAnalog(device->sck);
    toAnalog(device->mosi);
    toAnalog(device->miso);
    toAnalog(device->cs);
    if (tagLineIsValid(device->pwr))
    {
      palClearLine(device->pwr);
    }
    break;

  case TAG_SPI_OFF_CUSTOM:
    break;
  }

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

void tagI2cDeviceOn(const TagI2cDevice *device)
{
  if (device->mutex)
  {
    chBSemWait(device->mutex);
  }

  if (tagLineIsValid(device->pwr))
  {
    toOutput(device->pwr);
    palSetLine(device->pwr);
  }

  palSetLine(device->sda);
  palSetLine(device->scl);
  toOutput(device->scl);
  toOutput(device->sda);

  i2cStart(device->driver, &device->config);
}

void tagI2cDeviceOff(const TagI2cDevice *device)
{
  i2cStop(device->driver);

  if (tagLineIsValid(device->pwr))
  {
    palClearLine(device->pwr);
  }

  if (device->mutex)
  {
    chBSemSignal(device->mutex);
  }
}

void tagI2cDevicePrepareSleep(const TagI2cDevice *device)
{
  tagEnableStandbyPullup(device->scl);
  tagEnableStandbyPullup(device->sda);
  tagEnableStandbyPulldown(device->pwr);
}
