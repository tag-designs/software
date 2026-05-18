
#include "hal.h"
#include "custom.h"
#include "rtc_api.h"
#include "lps27hhw.h"
#include "lps.h"
#include "limits.h"
#include "sensor_io.h"

#define LPS27HW_ADR (0x5C)

#if defined(LPS_I2C) && defined(TAG_SENSOR_PRESSURE_LPS27)
#define LPS27_TIMEOUT 100

static const TagI2cRegisterIO lps27_i2c = {
  .driver = &I2CD1,
  .address = LPS27HW_ADR,
  .timeout = LPS27_TIMEOUT,
};
#define LPS27_REGISTER_DEVICE \
  { tagI2cReadRegister, tagI2cWriteRegister, &lps27_i2c }

#endif

#if defined(LPS_SPI) && defined(TAG_SENSOR_PRESSURE_LPS27)
static const TagStSpiRegisterIO lps27_spi = {
  .cs = LINE_STEVAL_CS,
  .read_mask = 0x80,
  .write_mask = 0x00,
};
#define LPS27_REGISTER_DEVICE \
  { tagStSpiReadRegister, tagStSpiWriteRegister, &lps27_spi }

#endif

#if defined(LPS_USART) && defined(TAG_SENSOR_PRESSURE_LPS27)
static const TagStUsartRegisterIO lps27_usart = {
  .usart = USART2,
  .cs = LINE_STEVAL_CS,
  .read_mask = 0x80,
  .write_mask = 0x00,
  .dummy = 0xff,
};
#define LPS27_REGISTER_DEVICE \
  { tagStUsartReadRegister, tagStUsartWriteRegister, &lps27_usart }

#endif

static const TagRegisterDevice lps27_registers = LPS27_REGISTER_DEVICE;

static void lps27_SetReg(enum LPS27_Reg reg, uint8_t *val, int num)
{
  (void)lps27_registers.write_register(lps27_registers.context, (uint8_t)reg,
                                       val, num);
}

static void lps27_GetReg(enum LPS27_Reg reg, uint8_t *val, int num)
{
  (void)lps27_registers.read_register(lps27_registers.context, (uint8_t)reg,
                                      val, num);
}

static inline void sleepMS(int ms) {
#if defined(LPS_SPI)
 //    SPI1->CR1 &= ~SPI_CR1_SPE;
 stopMilliseconds(true,ms);
#else
 stopMilliseconds(false,ms);
#endif
}

#if defined(TAG_SENSOR_PRESSURE_LPS27)

float lpsPressure(int16_t pressure) {
  return pressure/16.0f;
}

float lpsTemperature(int16_t temperature){
  return temperature/100.0f;
}


bool lpsGetPressureTemp(int16_t *pressure, int16_t *temperature)
{
 
  uint8_t status;
  uint8_t cmd[2];

  // default return values

  *pressure = SHRT_MIN;
  *temperature = SHRT_MIN;

  lpsOn();      // powered through gpio
  sleepMS(10);  // 10ms power up

  // set BDU and configure one shot

  cmd[0] = LPS27_CTRL_REG1_BDU;
  cmd[1] = LPS27_CTRL_REG2_ONE_SHOT |
           LPS27_CTRL_REG2_LOW_NOISE_EN |
           LPS27_CTRL_REG2_IF_ADD_INC;

  // write CTRL_REG1 and CTRL_REG2 to start one shot
  
  lps27_SetReg(LPS27_CTRL_REG1, cmd, 2);
    
  // wait for data

  for (int i = 0; i < 6; i++) {
    sleepMS(15);
    lps27_GetReg(LPS27_STATUS, &status, 1);
    if ((status & 3) == 3)
      break;
  }
  
  if (status == 3) // don't capture if overrun
  {
    lps27_GetReg(LPS27_PRESS_OUT_L, (uint8_t *) pressure, 2);
    lps27_GetReg(LPS27_TEMP_OUT_L, (uint8_t *) temperature, 2);
  }
  lpsOff();
  return status == 3;
}

bool lpsTest(void)
{
  uint8_t who;
  int16_t temperature,pressure;
  bool status;
  lpsOn();
  chThdSleepMilliseconds(10);
  lps27_GetReg(LPS27_WHO_AM_I, &who, 1);
  lpsOff();
  status = lpsGetPressureTemp(&pressure, &temperature);


  return (who == LPS27_WHO_AM_I_VALUE) && status;
}
#endif
