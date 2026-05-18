#include "hal.h"
#include "custom.h"
#include "rtc_api.h"
#include "lps22hhw.h"
#include "lps.h"
#include "limits.h"

static void lps22_SetReg(const TagPressureDevice *device, enum LPS22_Reg reg,
                         uint8_t *val, int num)
{
  (void)device->registers->write_register(device->registers->context,
                                          (uint8_t)reg, val, num);
}

static void lps22_GetReg(const TagPressureDevice *device, enum LPS22_Reg reg,
                         uint8_t *val, int num)
{
  (void)device->registers->read_register(device->registers->context,
                                         (uint8_t)reg, val, num);
}

float lps22Pressure(int16_t pressure) { return pressure / 16.0f; }

float lps22Temperature(int16_t temperature) { return temperature / 100.0f; }

bool lps22GetPressureTemp(const TagPressureDevice *device, int16_t *pressure,
                          int16_t *temperature)
{
  {
    // buffer to capture pressure/temperature;
    struct
    {
      int32_t pressure;
      int16_t temperature;
    } buf;
    uint8_t tmp;
    device->on();
#if defined(LPS_SPI)
    // SPI1->CR1 &= ~SPI_CR1_SPE;
#endif
    device->sleep_ms(5);
    //chThdSleepMilliseconds(true,2);
#if defined(LPS_SPI)
    // SPI1->CR1 |= SPI_CR1_SPE;
#endif
    uint8_t cmd = LPS22_CTRL_REG3_DRDY;
    lps22_SetReg(device, LPS22_CTRL_REG3, &cmd, 1);
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
     lps22_SetReg(device, LPS22_CTRL_REG2, &cmd, 1);
     
    // wait for data
    for (int i = 0; i < 6; i++)
    {
#if defined(LPS_SPI)
      //SPI1->CR1 &= ~SPI_CR1_SPE;
#endif
      device->sleep_ms(5);
      //chThdSleepMilliseconds(5);
#if defined(LPS_SPI)
      //SPI1->CR1 |= SPI_CR1_SPE;
#endif
      lps22_GetReg(device, LPS22_STATUS, &tmp, 1);
      if ((tmp & 3) == 3)
        break;
    }
    // read pressure/temperature
    lps22_GetReg(device, LPS22_PRESS_OUT_XL, &((uint8_t *)&buf)[1], 5);
    *pressure = buf.pressure >> 16;
    *temperature = buf.temperature;
    device->off();
    if (!(tmp & 1))
      *pressure = 0;
    if (!(tmp & 2))
      *temperature = SHRT_MIN;
    return (tmp & 3) == 3;
  }
  return false;
}

bool lps22Test(const TagPressureDevice *device)
{
  uint8_t who;
  device->on();
  device->sleep_ms(10);
  lps22_GetReg(device, LPS22_WHO_AM_I, &who, 1);
  device->off();
  return who == LPS22_WHO_AM_I_VALUE;
}
