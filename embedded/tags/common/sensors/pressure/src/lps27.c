
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
#include "debug_log.h"
#include "limits.h"
#include "phase_probe.h"

enum LPS27_Reg
{
    LPS27_WHO_AM_I = 0x0F,
    LPS27_CTRL_REG1 = 0x10,
    LPS27_CTRL_REG2 = 0x11,
    LPS27_STATUS = 0x27,
    LPS27_PRESS_OUT_XL = 0x28,
    LPS27_PRESS_OUT_L = 0x29,
    LPS27_TEMP_OUT_L = 0x2B,
};

#define LPS27_WHO_AM_I_VALUE (0xB3)

#define LPS27_CTRL_REG1_BDU (1 << 1)

#define LPS27_CTRL_REG2_ONE_SHOT (1 << 0)
#define LPS27_CTRL_REG2_LOW_NOISE_EN (1 << 1)
#define LPS27_CTRL_REG2_IF_ADD_INC (1 << 4)

#ifndef LPS27_POWERUP_MS
#define LPS27_POWERUP_MS 10U
#endif

#ifndef LPS27_READY_POLL_MS
#define LPS27_READY_POLL_MS 15U
#endif

#ifndef LPS27_READY_POLLS
#define LPS27_READY_POLLS 6
#endif

/**
 * @brief Readiness polls allowed for a low-noise conversion in
 *        lps27GetPressureTempFull(), LPS27_READY_POLL_MS apart.
 *
 * @details Low-noise conversions take longer than low-power ones, and a target
 *          may set LPS27_READY_POLLS as low as one for the low-power case.
 *          The loop leaves on the first ready status, so the allowance costs
 *          nothing when the data is ready sooner. Six polls of 5 ms sufficed
 *          for every low-noise sample on the bench (2026-11-09).
 */
#ifndef LPS27_LOW_NOISE_READY_POLLS
#define LPS27_LOW_NOISE_READY_POLLS 6
#endif

/** @name LPS27 register helpers
 * Register helpers keep the one-shot sampling path independent of the concrete
 * bus used by the pressure device descriptor. Each access opens and closes the
 * bus so STOP waits during power-up and conversion leave the bus pins parked.
 * @{
 */
/**
 * @brief Apply LPS27 sensor power without opening the register bus.
 *
 * @param[in] device Pressure device descriptor.
 */
static void lps27_PowerOn(const TagPressureDevice *device)
{
  tagBusPowerOn(&device->registers->bus);
}

/**
 * @brief Remove LPS27 sensor power and apply target-specific pin cleanup.
 *
 * @param[in] device Pressure device descriptor.
 */
static void lps27_PowerOff(const TagPressureDevice *device)
{
  tagBusPowerOff(&device->registers->bus);
  tagPressureDeviceAfterPowerOff(device);
}

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
  tagBusBegin(&device->registers->bus);
  (void)tagRegisterWrite(device->registers, (uint8_t)reg, val, num);
  tagBusEnd(&device->registers->bus);
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
  tagBusBegin(&device->registers->bus);
  (void)tagRegisterRead(device->registers, (uint8_t)reg, val, num);
  tagBusEnd(&device->registers->bus);
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
 
  uint8_t status = 0;
  uint8_t cmd[2];
  struct {
    int16_t pressure;
    int16_t temperature;
  } sample;

  // default return values

  *pressure = SHRT_MIN;
  *temperature = SHRT_MIN;

  lps27_PowerOn(device);
  tagPhaseProbeMark(1);   /* sensor rail on, bus session begun */
  stopMilliseconds(LPS27_POWERUP_MS);
  tagPhaseProbeMark(2);   /* power-up delay done */

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
  tagPhaseProbeMark(3);   /* one-shot triggered */
    
  // wait for data

  for (int i = 0; i < LPS27_READY_POLLS; i++) {
    stopMilliseconds(LPS27_READY_POLL_MS);
    lps27_GetReg(device, LPS27_STATUS, &status, 1);
    if ((status & 3) == 3)
      break;
  }
  
  tagPhaseProbeMark(4);   /* readiness poll finished */
  tagPhaseProbeAux(1, status);
  if (status == 3) // don't capture if overrun
  {
    lps27_GetReg(device, LPS27_PRESS_OUT_L, (uint8_t *)&sample,
                 sizeof(sample));
    *pressure = sample.pressure;
    *temperature = sample.temperature;
  }
  lps27_PowerOff(device);
  if (status != 3)
  {
    debug_log_printf("lps27: sample status=0x%x pressure=%d temp=%d\r\n",
                     status, *pressure, *temperature);
  }
  return status == 3;
}

/**
 * @brief Trigger one conversion and read all five output registers.
 *
 * @details Same one-shot sequence as lps27GetPressureTemp(), but the burst
 *          read starts at PRESS_OUT_XL (0x28), so the pressure keeps its low
 *          byte: 24 bits at 4096 LSB/hPa instead of 16 at 16 LSB/hPa. The
 *          noise mode is chosen per call rather than by LPS_LOW_POWER, so a
 *          stored configuration can select it. Readiness is polled up to
 *          LPS27_READY_POLLS times in low-power mode and
 *          LPS27_LOW_NOISE_READY_POLLS in low-noise mode, LPS27_READY_POLL_MS
 *          apart; the loop leaves on the first ready status.
 *
 *          lps27GetPressureTemp() is deliberately left as it was: it is the
 *          path qualified on PresTagRaw and BitPresTag, and PresTag still uses
 *          it when neither LPS27 option is configured.
 *
 * @param[in]  device    Pressure device descriptor.
 * @param[in]  low_noise true to set CTRL_REG2.LOW_NOISE_EN for this conversion.
 * @param[out] raw       PRESS_OUT_XL, PRESS_OUT_L, PRESS_OUT_H, TEMP_OUT_L,
 *                       TEMP_OUT_H as read. On failure the pressure bytes are
 *                       0x00 0x00 0x80 (24-bit minimum) and the temperature
 *                       0x8000, so a failed read is distinguishable from data
 *                       and from erased flash.
 * @return true when a fresh, non-overrun sample was read.
 *
 * @pre    The sensor is powered off (the call powers it and removes power).
 * @warning Blocks for the power-up delay and the readiness polls, sleeping in
 *          Stop between them; not callable from an ISR.
 */
bool lps27GetPressureTempFull(const TagPressureDevice *device, bool low_noise,
                              uint8_t raw[LPS27_FULL_SAMPLE_BYTES])
{
  uint8_t status = 0;
  uint8_t cmd[2];

  raw[0] = 0x00;
  raw[1] = 0x00;
  raw[2] = 0x80;
  raw[3] = 0x00;
  raw[4] = 0x80;

  lps27_PowerOn(device);
  tagPhaseProbeMark(1);   /* sensor rail on, bus session begun */
  stopMilliseconds(LPS27_POWERUP_MS);
  tagPhaseProbeMark(2);   /* power-up delay done */

  cmd[0] = LPS27_CTRL_REG1_BDU;
  cmd[1] = LPS27_CTRL_REG2_ONE_SHOT | LPS27_CTRL_REG2_IF_ADD_INC;
  if (low_noise)
    cmd[1] |= LPS27_CTRL_REG2_LOW_NOISE_EN;

  lps27_SetReg(device, LPS27_CTRL_REG1, cmd, 2);
  tagPhaseProbeMark(3);   /* one-shot triggered */

  const int polls = low_noise ? LPS27_LOW_NOISE_READY_POLLS : LPS27_READY_POLLS;
  for (int i = 0; i < polls; i++) {
    stopMilliseconds(LPS27_READY_POLL_MS);
    lps27_GetReg(device, LPS27_STATUS, &status, 1);
    if ((status & 3) == 3)
      break;
  }

  tagPhaseProbeMark(4);   /* readiness poll finished */
  tagPhaseProbeAux(1, status);
  if (status == 3) // don't capture if overrun
    lps27_GetReg(device, LPS27_PRESS_OUT_XL, raw, LPS27_FULL_SAMPLE_BYTES);
  lps27_PowerOff(device);
  if (status != 3)
    debug_log_printf("lps27: full sample status=0x%x\r\n", status);
  return status == 3;
}

/**
 * @brief Convert a full-resolution raw pressure to hectopascals.
 *
 * @param[in] pressure Sign-extended 24-bit raw pressure.
 * @return Pressure in hPa (4096 LSB/hPa).
 */
float lps27PressureFull(int32_t pressure)
{
  return (float)pressure / 4096.0f;
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
  lps27_PowerOn(device);
  stopMilliseconds(10);
  lps27_GetReg(device, LPS27_WHO_AM_I, &who, 1);
  lps27_PowerOff(device);
  status = lps27GetPressureTemp(device, &pressure, &temperature);

  if ((who != LPS27_WHO_AM_I_VALUE) || !status)
  {
    debug_log_printf("lps27: who=0x%x expected=0x%x status=%u\r\n",
                     who, LPS27_WHO_AM_I_VALUE, status ? 1U : 0U);
  }
  return (who == LPS27_WHO_AM_I_VALUE) && status;
}
/** @} */
