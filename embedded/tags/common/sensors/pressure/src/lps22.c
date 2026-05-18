#include "hal.h"
#include "custom.h"
#include "rtc_api.h"
#include "lps22hhw.h"
#include "lps.h"
#include "limits.h"
#include "sensor_io.h"

#define LPS22HW_ADR (0x5C)

#if defined(LPS_I2C) && defined(USE_LPS22)
#define LPS22_TIMEOUT 100

static const TagI2cRegisterIO lps22_i2c = {
  .driver = &I2CD1,
  .address = LPS22HW_ADR,
  .timeout = LPS22_TIMEOUT,
};

static void lps22_GetReg(enum LPS22_Reg reg, uint8_t *val, int num)
{
  (void)tagI2cReadRegister(&lps22_i2c, (uint8_t)reg, val, num);
}

static void lps22_SetReg(enum LPS22_Reg reg, uint8_t *val, int num)
{
  (void)tagI2cWriteRegister(&lps22_i2c, (uint8_t)reg, val, num);
}

#endif

#if defined(LPS_SPI) && defined(USE_LPS22)
// needs to be re-written to use three wire format
static const TagStSpiRegisterIO lps22_spi = {
  .cs = LINE_STEVAL_CS,
  .read_mask = 0x80,
  .write_mask = 0x00,
};

static void lps22_SetReg(enum LPS22_Reg reg, uint8_t *val, int num)
{
  (void)tagStSpiWriteRegister(&lps22_spi, (uint8_t)reg, val, num);
}

static void lps22_GetReg(enum LPS22_Reg reg, uint8_t *val, int num)
{
  (void)tagStSpiReadRegister(&lps22_spi, (uint8_t)reg, val, num);
}

#endif

#if defined(USE_LPS22)
bool lpsGetPressureTemp(int16_t *pressure, int16_t *temperature)
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
#if defined(LPS_SPI)
    // SPI1->CR1 &= ~SPI_CR1_SPE;
#endif
    stopMilliseconds(5);
    //chThdSleepMilliseconds(true,2);
#if defined(LPS_SPI)
    // SPI1->CR1 |= SPI_CR1_SPE;
#endif
    uint8_t cmd = LPS22_CTRL_REG3_DRDY;
    lps22_SetReg(LPS22_CTRL_REG3, &cmd, 1);
    //palEnableLineEvent(LINE_INT1,PAL_EVENT_MODE_RISING_EDGE);
#ifdef LPS_LOW_POWER
    cmd = LPS22_CTRL_REG2_ONE_SHOT |
          LPS22_CTRL_REG2_IF_ADD_INC;
#else
    cmd = LPS22_CTRL_REG2_ONE_SHOT |
          LPS22_CTRL_REG2_LOW_NOISE_EN |
          LPS22_CTRL_REG2_IF_ADD_INC;
#endif
    // start one-shot conversion
#if defined(LPS_SPI)
     SPI1->CR1 |= SPI_CR1_SPE;
#endif
     lps22_SetReg(LPS22_CTRL_REG2, &cmd, 1);
     
    // wait for data
    for (int i = 0; i < 6; i++)
    {
#if defined(LPS_SPI)
      //SPI1->CR1 &= ~SPI_CR1_SPE;
#endif
      stopMilliseconds(true,5);
      //chThdSleepMilliseconds(5);
#if defined(LPS_SPI)
      //SPI1->CR1 |= SPI_CR1_SPE;
#endif
      lps22_GetReg(LPS22_STATUS, &tmp, 1);
      if ((tmp & 3) == 3)
        break;
    }
    // read pressure/temperature
    lps22_GetReg(LPS22_PRESS_OUT_XL, &((uint8_t *)&buf)[1], 5);
    *pressure = buf.pressure >> 16;
    *temperature = buf.temperature;
    lpsOff();
    if (!(tmp & 1))
      *pressure = 0;
    if (!(tmp & 2))
      *temperature = SHRT_MIN;
    return (tmp & 3) == 3;
  }
  return false;
}

void lpsInit(void){}

bool lpsTest(void)
{
  uint8_t who;
  lpsOn();
  chThdSleepMilliseconds(10);
  lps22_GetReg(LPS22_WHO_AM_I, &who, 1);
  lpsOff();
  return who == LPS22_WHO_AM_I_VALUE;
}
#endif
