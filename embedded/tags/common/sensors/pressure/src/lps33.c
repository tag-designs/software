
#include "hal.h"
#include "custom.h"
#include "lps33hw.h"
#include "lps.h"
#include "limits.h"
#include "sensor_io.h"

#define LPS33HW_ADR (0x5C)

#ifdef LPS_I2C
#define LPS33_TIMEOUT 100

static const TagI2cRegisterIO lps33_i2c = {
  .driver = &I2CD1,
  .address = LPS33HW_ADR,
  .timeout = LPS33_TIMEOUT,
};
#define LPS33_REGISTER_DEVICE \
  { tagI2cReadRegister, tagI2cWriteRegister, &lps33_i2c }

#endif

#ifdef LPS_SPI
static const TagStSpiRegisterIO lps33_spi = {
  .cs = LINE_STEVAL_CS,
  .read_mask = 0x80,
  .write_mask = 0x00,
};
#define LPS33_REGISTER_DEVICE \
  { tagStSpiReadRegister, tagStSpiWriteRegister, &lps33_spi }

#endif

static const TagRegisterDevice lps33_registers = LPS33_REGISTER_DEVICE;

void lps33_SetReg(enum LPS33_Reg reg, unsigned char *val, int num)
{
  (void)lps33_registers.write_register(lps33_registers.context, (uint8_t)reg,
                                       val, num);
}

void lps33_GetReg(enum LPS33_Reg reg, uint8_t *val, int num)
{
  (void)lps33_registers.read_register(lps33_registers.context, (uint8_t)reg,
                                      val, num);
}

#if defined(USE_LPS33)
bool GetPressureTemp(int16_t *pressure, int16_t *temperature)
{
  {

    // buffer to capture pressure/temperature;
    struct
    {
      int32_t pressure;
      int16_t temperature;
    } buf;
    uint8_t tmp;
    lpsOn();
    // wait for power to stabilize
    chThdSleepMilliseconds(10);
#ifdef LPS_LOW_POWER
    /* enable low power converion
         read/write to avoid modifying 
         reserved bit */
    lps33_GetReg(LPS33_RES_CONF, &tmp, 1);
    tmp |= LPS33_RES_CONF_LC_EN;
    lps33_SetReg(LPS33_RES_CONF, &tmp, 1);
#endif
    // start one-shot conversion
    uint8_t cmd = LPS33_CTRL_REG2_ONE_SHOT |
                  LPS33_CTRL_REG2_IF_ADD_INC;
    lps33_SetReg(LPS33_CTRL_REG2, &cmd, 1);
    // wait for data
    chThdSleepMilliseconds(20);
    lps33_GetReg(LPS33_STATUS, &tmp, 1);

    if ((tmp & 3) == 3)
    {
      // read pressure/temperature
      lps33_GetReg(LPS33_PRESS_OUT_XL, &((uint8_t *)&buf)[1], 5);
      *pressure = buf.pressure >> 16;
      *temperature = buf.temperature;
    }
    else
    {
      *pressure = 0;
      *temperature = SHRT_MIN;
    }
    lpsOff();

    return (tmp & 3) == 3;
  }
  return false;
}

bool lpsTest(void)
{
  uint8_t who;
  lpsOn();
  chThdSleepMilliseconds(10);
  lps33_GetReg(LPS33_WHO_AM_I, &who, 1);
  lpsOff();
  return who == LPS33_WHO_AM_I_VALUE;
}
#endif
