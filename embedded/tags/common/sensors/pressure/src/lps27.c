
#include "hal.h"
#include "custom.h"
#include "rtc_api.h"
#include "lps27hhw.h"
#include "lps.h"
#include "limits.h"

static void lps27_SetReg(const TagPressureDevice *device, enum LPS27_Reg reg,
                         uint8_t *val, int num)
{
  (void)tagRegisterWrite(device->registers, (uint8_t)reg, val, num);
}

static void lps27_GetReg(const TagPressureDevice *device, enum LPS27_Reg reg,
                         uint8_t *val, int num)
{
  (void)tagRegisterRead(device->registers, (uint8_t)reg, val, num);
}

float lps27Pressure(int16_t pressure) {
  return pressure/16.0f;
}

float lps27Temperature(int16_t temperature){
  return temperature/100.0f;
}


bool lps27GetPressureTemp(const TagPressureDevice *device, int16_t *pressure,
                          int16_t *temperature)
{
 
  uint8_t status;
  uint8_t cmd[2];

  // default return values

  *pressure = SHRT_MIN;
  *temperature = SHRT_MIN;

  tagPressureDeviceBegin(device);         // powered through gpio
  device->sleep_ms(10); // 10ms power up

  // set BDU and configure one shot

  cmd[0] = LPS27_CTRL_REG1_BDU;
  cmd[1] = LPS27_CTRL_REG2_ONE_SHOT |
           LPS27_CTRL_REG2_LOW_NOISE_EN |
           LPS27_CTRL_REG2_IF_ADD_INC;

  // write CTRL_REG1 and CTRL_REG2 to start one shot
  
  lps27_SetReg(device, LPS27_CTRL_REG1, cmd, 2);
    
  // wait for data

  for (int i = 0; i < 6; i++) {
    device->sleep_ms(15);
    lps27_GetReg(device, LPS27_STATUS, &status, 1);
    if ((status & 3) == 3)
      break;
  }
  
  if (status == 3) // don't capture if overrun
  {
    lps27_GetReg(device, LPS27_PRESS_OUT_L, (uint8_t *) pressure, 2);
    lps27_GetReg(device, LPS27_TEMP_OUT_L, (uint8_t *) temperature, 2);
  }
  tagPressureDeviceEnd(device);
  return status == 3;
}

bool lps27Test(const TagPressureDevice *device)
{
  uint8_t who;
  int16_t temperature,pressure;
  bool status;
  tagPressureDeviceBegin(device);
  device->sleep_ms(10);
  lps27_GetReg(device, LPS27_WHO_AM_I, &who, 1);
  tagPressureDeviceEnd(device);
  status = lps27GetPressureTemp(device, &pressure, &temperature);


  return (who == LPS27_WHO_AM_I_VALUE) && status;
}
