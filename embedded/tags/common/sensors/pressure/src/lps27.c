
/**
 * @file lps27.c
 * @brief Descriptor-backed ST LPS27HHW one-shot pressure sensor driver.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"
#include "custom.h"
#include "rtc_api.h"
#include "lps.h"
#include "lps27hhw.h"
#include "limits.h"

enum LPS27_Reg
{
    LPS27_WHO_AM_I = 0x0F,
    LPS27_CTRL_REG1 = 0x10,
    LPS27_CTRL_REG2 = 0x11,
    LPS27_STATUS = 0x27,
    LPS27_PRESS_OUT_L = 0x29,
    LPS27_TEMP_OUT_L = 0x2B,
};

#define LPS27_WHO_AM_I_VALUE (0xB3)

#define LPS27_CTRL_REG1_BDU (1 << 1)

#define LPS27_CTRL_REG2_ONE_SHOT (1 << 0)
#define LPS27_CTRL_REG2_LOW_NOISE_EN (1 << 1)
#define LPS27_CTRL_REG2_IF_ADD_INC (1 << 4)

/** @name LPS27 register helpers
 * Register helpers keep the one-shot sampling path independent of the concrete
 * bus used by the pressure device descriptor.
 * @{
 */
/**
 * @brief Write one or more LPS27 registers.
 *
 * @param[in] device Pressure device descriptor.
 * @param[in] reg First register to write.
 * @param[in] val Source buffer.
 * @param[in] num Number of bytes to write.
 */
static void lps27_SetReg(const TagPressureDevice *device, enum LPS27_Reg reg,
                         uint8_t *val, int num)
{
  (void)tagRegisterWrite(device->registers, (uint8_t)reg, val, num);
}

/**
 * @brief Read one or more LPS27 registers.
 *
 * @param[in] device Pressure device descriptor.
 * @param[in] reg First register to read.
 * @param[out] val Destination buffer.
 * @param[in] num Number of bytes to read.
 */
static void lps27_GetReg(const TagPressureDevice *device, enum LPS27_Reg reg,
                         uint8_t *val, int num)
{
  (void)tagRegisterRead(device->registers, (uint8_t)reg, val, num);
}
/** @} */

/** @name LPS27 conversion helpers
 * Convert raw LPS27 outputs into engineering units.
 * @{
 */
/**
 * @brief Convert raw pressure to hectopascals.
 *
 * @param[in] pressure Raw pressure output.
 * @return Pressure in hPa.
 */
float lps27Pressure(int16_t pressure) {
  return pressure/16.0f;
}

/**
 * @brief Convert raw temperature to degrees Celsius.
 *
 * @param[in] temperature Raw temperature output.
 * @return Temperature in degrees Celsius.
 */
float lps27Temperature(int16_t temperature){
  return temperature/100.0f;
}
/** @} */

/** @name LPS27 sampling and test
 * Descriptor-backed one-shot sampling and identity test.
 * @{
 */
/**
 * @brief Trigger and read one pressure/temperature sample.
 *
 * @param[in] device Pressure device descriptor.
 * @param[out] pressure Raw pressure output.
 * @param[out] temperature Raw temperature output.
 * @return true when a fresh sample was read.
 */
bool lps27GetPressureTemp(const TagPressureDevice *device, int16_t *pressure,
                          int16_t *temperature)
{
 
  uint8_t status;
  uint8_t cmd[2];

  // default return values

  *pressure = SHRT_MIN;
  *temperature = SHRT_MIN;

  tagPressureDeviceBegin(device);         // powered through gpio
  stopMilliseconds(10); // 10ms power up

  // set BDU and configure one shot

  cmd[0] = LPS27_CTRL_REG1_BDU;
#if defined(LPS_LOW_POWER) && (LPS_LOW_POWER)
  cmd[1] = LPS27_CTRL_REG2_ONE_SHOT |
           LPS27_CTRL_REG2_IF_ADD_INC;
#else
  cmd[1] = LPS27_CTRL_REG2_ONE_SHOT |
           LPS27_CTRL_REG2_LOW_NOISE_EN |
           LPS27_CTRL_REG2_IF_ADD_INC;    
#endif

  // write CTRL_REG1 and CTRL_REG2 to start one shot
  
  lps27_SetReg(device, LPS27_CTRL_REG1, cmd, 2);
    
  // wait for data

  for (int i = 0; i < 6; i++) {
    stopMilliseconds(15);
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

/**
 * @brief Verify identity and sample readiness for an LPS27HHW device.
 *
 * @param[in] device Pressure device descriptor.
 * @return true when identity and sample read pass.
 */
bool lps27Test(const TagPressureDevice *device)
{
  uint8_t who;
  int16_t temperature,pressure;
  bool status;
  tagPressureDeviceBegin(device);
  stopMilliseconds(10);
  lps27_GetReg(device, LPS27_WHO_AM_I, &who, 1);
  tagPressureDeviceEnd(device);
  status = lps27GetPressureTemp(device, &pressure, &temperature);


  return (who == LPS27_WHO_AM_I_VALUE) && status;
}
/** @} */
