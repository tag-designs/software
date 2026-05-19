#include "power.h"

#include "gpio_utils.h"

#define TAG_I2C_REGISTER_MAX_WRITE 16

/*
 * Conventional register-oriented I2C transfers shared by sensors and RTCs.
 * This file owns I2C device/session setup plus conventional register-oriented
 * transactions. The device descriptor carries the tag-specific lines and
 * ChibiOS/fallback I2C configuration; the register helpers assume the bus has
 * already been enabled by tagI2cDeviceOn().
 */

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

int tagI2cWriteRegister(const void *io, uint8_t reg, const uint8_t *buf,
                        uint32_t len)
{
  const TagI2cRegisterBus *i2c = (const TagI2cRegisterBus *)io;
  uint8_t txbuf[TAG_I2C_REGISTER_MAX_WRITE + 1];

  if (len > TAG_I2C_REGISTER_MAX_WRITE) {
    return MSG_RESET;
  }

  txbuf[0] = reg;
  for (uint32_t i = 0; i < len; i++) {
    txbuf[i + 1] = buf[i];
  }

  return i2cMasterTransmitTimeout(i2c->driver, i2c->address, txbuf, len + 1,
                                  0, 0, i2c->timeout);
}

int tagI2cReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                       uint32_t len)
{
  const TagI2cRegisterBus *i2c = (const TagI2cRegisterBus *)io;

  return i2cMasterTransmitTimeout(i2c->driver, i2c->address, &reg, 1, buf,
                                  len, i2c->timeout);
}
