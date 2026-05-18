
#include "hal.h"
#include "custom.h"
#include "lps33hw.h"
#include "lps.h"
#include "limits.h"
static void lps33_SetRegDevice(const TagPressureDevice *device,
                               enum LPS33_Reg reg, unsigned char *val, int num)
{
  (void)device->registers->write_register(device->registers->context,
                                          (uint8_t)reg, val, num);
}

static void lps33_GetRegDevice(const TagPressureDevice *device,
                               enum LPS33_Reg reg, uint8_t *val, int num)
{
  (void)device->registers->read_register(device->registers->context,
                                         (uint8_t)reg, val, num);
}

float lps33Pressure(int16_t pressure) { return pressure / 16.0f; }

float lps33Temperature(int16_t temperature) { return temperature / 100.0f; }

bool lps33GetPressureTemp(const TagPressureDevice *device, int16_t *pressure,
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
    tagPressureDeviceBegin(device);
    // wait for power to stabilize
    device->sleep_ms(10);
#ifdef LPS_LOW_POWER
    /* enable low power converion
         read/write to avoid modifying 
         reserved bit */
    lps33_GetRegDevice(device, LPS33_RES_CONF, &tmp, 1);
    tmp |= LPS33_RES_CONF_LC_EN;
    lps33_SetRegDevice(device, LPS33_RES_CONF, &tmp, 1);
#endif
    // start one-shot conversion
    uint8_t cmd = LPS33_CTRL_REG2_ONE_SHOT |
                  LPS33_CTRL_REG2_IF_ADD_INC;
    lps33_SetRegDevice(device, LPS33_CTRL_REG2, &cmd, 1);
    // wait for data
    device->sleep_ms(20);
    lps33_GetRegDevice(device, LPS33_STATUS, &tmp, 1);

    if ((tmp & 3) == 3)
    {
      // read pressure/temperature
      lps33_GetRegDevice(device, LPS33_PRESS_OUT_XL, &((uint8_t *)&buf)[1], 5);
      *pressure = buf.pressure >> 16;
      *temperature = buf.temperature;
    }
    else
    {
      *pressure = 0;
      *temperature = SHRT_MIN;
    }
    tagPressureDeviceEnd(device);

    return (tmp & 3) == 3;
  }
  return false;
}

bool lps33Test(const TagPressureDevice *device)
{
  uint8_t who;
  tagPressureDeviceBegin(device);
  device->sleep_ms(10);
  lps33_GetRegDevice(device, LPS33_WHO_AM_I, &who, 1);
  tagPressureDeviceEnd(device);
  return who == LPS33_WHO_AM_I_VALUE;
}
