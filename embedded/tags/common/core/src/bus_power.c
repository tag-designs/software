#include "power.h"

#include "gpio_utils.h"
#include "spi_bus.h"
#include "usart_bus.h"

/*
 * Shared bus and standby-pin helpers.
 *
 * Device modules describe their board-provided LINE_xxx pins with small
 * descriptors. These helpers provide common device power sequencing and
 * standby pull configuration; bus-specific modules own controller state and
 * register-level stop/resume handling.
 */

bool tagLineIsValid(ioline_t line)
{
  return line != TAG_NO_LINE;
}

void tagDisableActiveBusesForStop(void)
{
  tagSpiDisableActiveForStop();
  tagUsartDisableActiveForStop();
}

void tagEnableActiveBusesAfterStop(void)
{
  tagSpiEnableActiveAfterStop();
  tagUsartEnableActiveAfterStop();
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

  toAlternate(device->sck);
  toAlternate(device->miso);
  toAlternate(device->mosi);

  if (device->controller && device->controller->enable)
  {
    device->controller->enable();
  }
}

void tagSpiBusEnd(const TagSpiDevice *device)
{
  palSetLine(device->cs);

  if (device->controller && device->controller->disable)
  {
    device->controller->disable();
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
