#include "i2c_bus.h"

#define TAG_I2C_REGISTER_MAX_WRITE 16

/*
 * Conventional register-oriented I2C transfers shared by sensors and RTCs.
 * Power sequencing, mutex ownership, and fallback-I2C line configuration stay
 * in the tag/family device descriptor; this file only knows how to emit
 * register-addressed transactions once the bus is available.
 */
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
