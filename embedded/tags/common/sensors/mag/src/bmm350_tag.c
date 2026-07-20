/**
 * @file bmm350_tag.c
 * @brief Native BMM350 magnetometer driver for tag firmware.
 * @author tag firmware authors
 * @date 2026-07-17
 *
 * Portions of the register definitions, OTP decoding, and floating-point
 * compensation math are adapted from Bosch Sensortec's BMM350 SensorAPI
 * v1.10.0, licensed under the BSD-3-Clause license:
 *
 * Copyright (c) 2025 Bosch Sensortec GmbH. All rights reserved.
 *
 * Maintainer notes:
 * - Public entry points assume the caller has opened a device session with
 *   bmm350DeviceBegin(). This mirrors the AK09940A driver style and keeps bus
 *   arbitration visible in family sensor code.
 * - BMM350 I2C reads return two dummy bytes before payload data. Keep
 *   BMM350_I2C_DUMMY_BYTES in sync with the read helper; changing it without
 *   auditing all burst reads will corrupt chip ID, OTP, and sample data.
 * - bmm350InitContinuous() intentionally reloads OTP compensation on every
 *   initialization. This costs startup time, but avoids stale compensation RAM
 *   after resets and keeps calibration-state and collection-state behavior
 *   identical.
 * - The driver clears the latched DRDY output by reading INT_STATUS after the
 *   12-byte sample burst. Do not remove that read unless the interrupt mode is
 *   changed in bmm350ConfigureInterrupt().
 */

#include "bmm350_tag.h"

#include "power.h"
#include "sensor_io.h"
#include "timekeeping.h"

#include <stddef.h>

#define BMM350_CMD_SOFTRESET 0xB6U

#define BMM350_REG_CHIP_ID 0x00U
#define BMM350_REG_PMU_CMD_AGGR_SET 0x04U
#define BMM350_REG_PMU_CMD 0x06U
#define BMM350_REG_INT_CTRL 0x2EU
#define BMM350_REG_INT_STATUS 0x30U
#define BMM350_REG_MAG_X_XLSB 0x31U
#define BMM350_REG_OTP_CMD_REG 0x50U
#define BMM350_REG_OTP_DATA_MSB_REG 0x52U
#define BMM350_REG_OTP_DATA_LSB_REG 0x53U
#define BMM350_REG_OTP_STATUS_REG 0x55U
#define BMM350_REG_CMD 0x7EU

#define BMM350_I2C_DUMMY_BYTES 2U
#define BMM350_MAG_TEMP_DATA_LEN 12U
#define BMM350_MAX_READ_LEN (BMM350_MAG_TEMP_DATA_LEN + BMM350_I2C_DUMMY_BYTES)

#define BMM350_PMU_CMD_SUS 0x00U
#define BMM350_PMU_CMD_NM 0x01U
#define BMM350_PMU_CMD_UPD_OAE 0x02U

#define BMM350_AVG_POS 4U
#define BMM350_AVG_MSK 0x30U
#define BMM350_ODR_MSK 0x0FU

#define BMM350_INT_MODE_LATCHED 0x01U
#define BMM350_INT_MODE_POS 0U
#define BMM350_INT_POL_POS 1U
#define BMM350_INT_OD_POS 2U
#define BMM350_INT_OUTPUT_EN_POS 3U
#define BMM350_DRDY_DATA_REG_EN_POS 7U

#define BMM350_OTP_DATA_LENGTH 32U
#define BMM350_OTP_CMD_DIR_READ 0x20U
#define BMM350_OTP_CMD_PWR_OFF_OTP 0x80U
#define BMM350_OTP_WORD_ADDR_MSK 0x1FU
#define BMM350_OTP_STATUS_ERROR_MSK 0xE0U
#define BMM350_OTP_STATUS_CMD_DONE 0x01U

#define BMM350_TEMP_OFF_SENS 0x0DU
#define BMM350_MAG_OFFSET_X 0x0EU
#define BMM350_MAG_OFFSET_Y 0x0FU
#define BMM350_MAG_OFFSET_Z 0x10U
#define BMM350_MAG_SENS_X 0x10U
#define BMM350_MAG_SENS_Y 0x11U
#define BMM350_MAG_SENS_Z 0x11U
#define BMM350_MAG_TCO_X 0x12U
#define BMM350_MAG_TCO_Y 0x13U
#define BMM350_MAG_TCO_Z 0x14U
#define BMM350_MAG_TCS_X 0x12U
#define BMM350_MAG_TCS_Y 0x13U
#define BMM350_MAG_TCS_Z 0x14U
#define BMM350_CROSS_X_Y 0x15U
#define BMM350_CROSS_Y_X 0x15U
#define BMM350_CROSS_Z_X 0x16U
#define BMM350_CROSS_Z_Y 0x16U
#define BMM350_MAG_DUT_T_0 0x18U

#define BMM350_LSB_MASK 0x00FFU
#define BMM350_MSB_MASK 0xFF00U

#define BMM350_START_UP_TIME_FROM_POR_US 3000U
#define BMM350_SOFT_RESET_DELAY_US 24000U
#define BMM350_OTP_POLL_DELAY_US 300U
#define BMM350_OTP_POLL_LIMIT 100U
#define BMM350_UPD_OAE_DELAY_US 1000U
#define BMM350_GOTO_SUSPEND_DELAY_US 6000U
#define BMM350_SUSPEND_TO_NORMAL_DELAY_US 38000U

static void bmm350DelayUs(uint32_t delay_us)
{
  if (delay_us >= 1000U)
    stopMilliseconds((delay_us + 999U) / 1000U);
  else
    chThdSleepMicroseconds(delay_us);
}

static int32_t bmm350FixSign(uint32_t value, uint8_t bits)
{
  uint32_t sign = 1UL << (bits - 1U);
  uint32_t mask = (1UL << bits) - 1UL;
  value &= mask;
  if ((value & sign) != 0U)
    return (int32_t)(value - (1UL << bits));
  return (int32_t)value;
}

static msg_t bmm350WriteRegister(const TagBmm350Device *dev, uint8_t reg,
                                 const uint8_t *data, uint32_t len)
{
  if (dev == NULL || dev->i2c == NULL || data == NULL || len == 0U)
    return MSG_RESET;
  return tagI2cWriteRegister(dev->i2c, reg, data, len);
}

static msg_t bmm350ReadRegister(const TagBmm350Device *dev, uint8_t reg,
                                uint8_t *data, uint32_t len)
{
  uint8_t rx[BMM350_MAX_READ_LEN];

  if (dev == NULL || dev->i2c == NULL || data == NULL)
    return MSG_RESET;
  if ((len + BMM350_I2C_DUMMY_BYTES) > sizeof(rx))
    return MSG_RESET;

  if (tagI2cReadRegister(dev->i2c, reg, rx,
                         len + BMM350_I2C_DUMMY_BYTES) != MSG_OK)
    return MSG_RESET;

  for (uint32_t i = 0; i < len; i++)
    data[i] = rx[i + BMM350_I2C_DUMMY_BYTES];
  return MSG_OK;
}

static msg_t bmm350WriteU8(const TagBmm350Device *dev, uint8_t reg,
                           uint8_t value)
{
  return bmm350WriteRegister(dev, reg, &value, 1U);
}

static msg_t bmm350ReadU8(const TagBmm350Device *dev, uint8_t reg,
                          uint8_t *value)
{
  return bmm350ReadRegister(dev, reg, value, 1U);
}

static bmm350_performance_t bmm350ClampPerformance(
    bmm350_rate_t rate, bmm350_performance_t performance)
{
  if (rate == BMM350_RATE_400HZ && performance >= BMM350_PERF_REGULAR)
    return BMM350_PERF_LOW_POWER;
  if (rate == BMM350_RATE_200HZ && performance >= BMM350_PERF_LOW_NOISE)
    return BMM350_PERF_REGULAR;
  if (rate == BMM350_RATE_100HZ && performance >= BMM350_PERF_ULTRA_LOW_NOISE)
    return BMM350_PERF_LOW_NOISE;
  return performance;
}

static msg_t bmm350SetOdrPerformance(const TagBmm350Device *dev,
                                     bmm350_rate_t rate,
                                     bmm350_performance_t performance)
{
  uint8_t reg = ((uint8_t)rate & BMM350_ODR_MSK);
  performance = bmm350ClampPerformance(rate, performance);
  reg = (uint8_t)((reg & (uint8_t)~BMM350_AVG_MSK) |
                  (((uint8_t)performance << BMM350_AVG_POS) &
                   BMM350_AVG_MSK));

  if (bmm350WriteU8(dev, BMM350_REG_PMU_CMD_AGGR_SET, reg) != MSG_OK)
    return MSG_RESET;
  if (bmm350WriteU8(dev, BMM350_REG_PMU_CMD, BMM350_PMU_CMD_UPD_OAE) != MSG_OK)
    return MSG_RESET;
  bmm350DelayUs(BMM350_UPD_OAE_DELAY_US);
  return MSG_OK;
}

static msg_t bmm350ConfigureInterrupt(const TagBmm350Device *dev)
{
  uint8_t reg = 0;

  if (bmm350ReadU8(dev, BMM350_REG_INT_CTRL, &reg) != MSG_OK)
    return MSG_RESET;

  reg &= (uint8_t)~((1U << BMM350_INT_MODE_POS) |
                    (1U << BMM350_INT_POL_POS) |
                    (1U << BMM350_INT_OD_POS) |
                    (1U << BMM350_INT_OUTPUT_EN_POS) |
                    (1U << BMM350_DRDY_DATA_REG_EN_POS));
  reg |= (uint8_t)(BMM350_INT_MODE_LATCHED << BMM350_INT_MODE_POS);
  reg |= (uint8_t)((uint8_t)dev->interrupt_polarity << BMM350_INT_POL_POS);
  reg |= (uint8_t)((uint8_t)dev->interrupt_drive << BMM350_INT_OD_POS);
  reg |= (uint8_t)(1U << BMM350_INT_OUTPUT_EN_POS);
  reg |= (uint8_t)(1U << BMM350_DRDY_DATA_REG_EN_POS);

  return bmm350WriteU8(dev, BMM350_REG_INT_CTRL, reg);
}

static msg_t bmm350ReadOtpWord(const TagBmm350Device *dev, uint8_t addr,
                               uint16_t *word)
{
  uint8_t command = (uint8_t)(BMM350_OTP_CMD_DIR_READ |
                              (addr & BMM350_OTP_WORD_ADDR_MSK));
  uint8_t status = 0;
  uint8_t msb = 0;
  uint8_t lsb = 0;

  if (word == NULL)
    return MSG_RESET;
  if (bmm350WriteU8(dev, BMM350_REG_OTP_CMD_REG, command) != MSG_OK)
    return MSG_RESET;

  for (uint32_t i = 0; i < BMM350_OTP_POLL_LIMIT; i++) {
    bmm350DelayUs(BMM350_OTP_POLL_DELAY_US);
    if (bmm350ReadU8(dev, BMM350_REG_OTP_STATUS_REG, &status) != MSG_OK)
      return MSG_RESET;
    if ((status & BMM350_OTP_STATUS_ERROR_MSK) != 0U)
      return MSG_RESET;
    if ((status & BMM350_OTP_STATUS_CMD_DONE) != 0U) {
      if (bmm350ReadU8(dev, BMM350_REG_OTP_DATA_MSB_REG, &msb) != MSG_OK)
        return MSG_RESET;
      if (bmm350ReadU8(dev, BMM350_REG_OTP_DATA_LSB_REG, &lsb) != MSG_OK)
        return MSG_RESET;
      *word = (uint16_t)(((uint16_t)msb << 8) | lsb);
      return MSG_OK;
    }
  }
  return MSG_RESET;
}

static void bmm350UpdateCompensation(TagBmm350Compensation *comp)
{
  uint16_t off_x;
  uint16_t off_y;
  uint16_t off_z;
  uint16_t t_off;
  uint8_t sens_x;
  uint8_t sens_y;
  uint8_t sens_z;
  uint8_t t_sens;
  uint8_t tco_x;
  uint8_t tco_y;
  uint8_t tco_z;
  uint8_t tcs_x;
  uint8_t tcs_y;
  uint8_t tcs_z;
  uint8_t cross_x_y;
  uint8_t cross_y_x;
  uint8_t cross_z_x;
  uint8_t cross_z_y;

  off_x = comp->otp_data[BMM350_MAG_OFFSET_X] & 0x0FFFU;
  off_y = (uint16_t)(((comp->otp_data[BMM350_MAG_OFFSET_X] & 0xF000U) >> 4) +
                     (comp->otp_data[BMM350_MAG_OFFSET_Y] & BMM350_LSB_MASK));
  off_z = (uint16_t)((comp->otp_data[BMM350_MAG_OFFSET_Y] & 0x0F00U) +
                     (comp->otp_data[BMM350_MAG_OFFSET_Z] & BMM350_LSB_MASK));
  t_off = comp->otp_data[BMM350_TEMP_OFF_SENS] & BMM350_LSB_MASK;

  comp->offset.offset_x = (float)bmm350FixSign(off_x, 12);
  comp->offset.offset_y = (float)bmm350FixSign(off_y, 12);
  comp->offset.offset_z = (float)bmm350FixSign(off_z, 12);
  comp->offset.t_offs = (float)bmm350FixSign(t_off, 8) / 5.0f;

  sens_x = (uint8_t)((comp->otp_data[BMM350_MAG_SENS_X] & BMM350_MSB_MASK) >> 8);
  sens_y = (uint8_t)(comp->otp_data[BMM350_MAG_SENS_Y] & BMM350_LSB_MASK);
  sens_z = (uint8_t)((comp->otp_data[BMM350_MAG_SENS_Z] & BMM350_MSB_MASK) >> 8);
  t_sens = (uint8_t)((comp->otp_data[BMM350_TEMP_OFF_SENS] & BMM350_MSB_MASK) >> 8);

  comp->sensitivity.sens_x = (float)bmm350FixSign(sens_x, 8) / 256.0f;
  comp->sensitivity.sens_y = (float)bmm350FixSign(sens_y, 8) / 256.0f;
  comp->sensitivity.sens_z = (float)bmm350FixSign(sens_z, 8) / 256.0f;
  comp->sensitivity.t_sens = (float)bmm350FixSign(t_sens, 8) / 512.0f;

  tco_x = (uint8_t)(comp->otp_data[BMM350_MAG_TCO_X] & BMM350_LSB_MASK);
  tco_y = (uint8_t)(comp->otp_data[BMM350_MAG_TCO_Y] & BMM350_LSB_MASK);
  tco_z = (uint8_t)(comp->otp_data[BMM350_MAG_TCO_Z] & BMM350_LSB_MASK);

  comp->tco.tco_x = (float)bmm350FixSign(tco_x, 8) / 32.0f;
  comp->tco.tco_y = (float)bmm350FixSign(tco_y, 8) / 32.0f;
  comp->tco.tco_z = (float)bmm350FixSign(tco_z, 8) / 32.0f;

  tcs_x = (uint8_t)((comp->otp_data[BMM350_MAG_TCS_X] & BMM350_MSB_MASK) >> 8);
  tcs_y = (uint8_t)((comp->otp_data[BMM350_MAG_TCS_Y] & BMM350_MSB_MASK) >> 8);
  tcs_z = (uint8_t)((comp->otp_data[BMM350_MAG_TCS_Z] & BMM350_MSB_MASK) >> 8);

  comp->tcs.tcs_x = (float)bmm350FixSign(tcs_x, 8) / 16384.0f;
  comp->tcs.tcs_y = (float)bmm350FixSign(tcs_y, 8) / 16384.0f;
  comp->tcs.tcs_z = (float)bmm350FixSign(tcs_z, 8) / 16384.0f;

  comp->t0 = ((float)bmm350FixSign(comp->otp_data[BMM350_MAG_DUT_T_0], 16) /
              512.0f) + 23.0f;

  cross_x_y = (uint8_t)(comp->otp_data[BMM350_CROSS_X_Y] & BMM350_LSB_MASK);
  cross_y_x = (uint8_t)((comp->otp_data[BMM350_CROSS_Y_X] & BMM350_MSB_MASK) >> 8);
  cross_z_x = (uint8_t)(comp->otp_data[BMM350_CROSS_Z_X] & BMM350_LSB_MASK);
  cross_z_y = (uint8_t)((comp->otp_data[BMM350_CROSS_Z_Y] & BMM350_MSB_MASK) >> 8);

  comp->cross_axis.cross_x_y = (float)bmm350FixSign(cross_x_y, 8) / 800.0f;
  comp->cross_axis.cross_y_x = (float)bmm350FixSign(cross_y_x, 8) / 800.0f;
  comp->cross_axis.cross_z_x = (float)bmm350FixSign(cross_z_x, 8) / 800.0f;
  comp->cross_axis.cross_z_y = (float)bmm350FixSign(cross_z_y, 8) / 800.0f;
}

static void bmm350LsbToUTAndDegC(float coeff[4])
{
  const float bxy_sens = 14.55f;
  const float bz_sens = 9.0f;
  const float temp_sens = 0.00204f;
  const float ina_xy_gain_trgt = 19.46f;
  const float ina_z_gain_trgt = 31.0f;
  const float adc_gain = 1.0f / 1.5f;
  const float lut_gain = 0.714607238769531f;
  const float power = 1000000.0f / 1048576.0f;

  coeff[0] = power / (bxy_sens * ina_xy_gain_trgt * adc_gain * lut_gain);
  coeff[1] = power / (bxy_sens * ina_xy_gain_trgt * adc_gain * lut_gain);
  coeff[2] = power / (bz_sens * ina_z_gain_trgt * adc_gain * lut_gain);
  coeff[3] = 1.0f / (temp_sens * adc_gain * lut_gain * 1048576.0f);
}

void bmm350DeviceBegin(const TagBmm350Device *dev)
{
  if (dev == NULL || dev->i2c == NULL)
    return;

  tagI2cDevicePowerOn(dev->i2c);
  if (tagLineIsValid(dev->i2c->pwr))
    bmm350DelayUs(BMM350_START_UP_TIME_FROM_POR_US);
  tagI2cBusBegin(dev->i2c);
}

void bmm350DeviceEnd(const TagBmm350Device *dev)
{
  if (dev == NULL || dev->i2c == NULL)
    return;

  tagI2cBusEnd(dev->i2c);
  tagI2cDevicePowerOff(dev->i2c);
}

bool bmm350CheckWhoami(const TagBmm350Device *dev)
{
  uint8_t chip_id = 0;
  return bmm350ReadU8(dev, BMM350_REG_CHIP_ID, &chip_id) == MSG_OK &&
         chip_id == BMM350_CHIP_ID_VALUE;
}

msg_t bmm350Reset(const TagBmm350Device *dev)
{
  if (bmm350WriteU8(dev, BMM350_REG_CMD, BMM350_CMD_SOFTRESET) != MSG_OK)
    return MSG_RESET;
  bmm350DelayUs(BMM350_SOFT_RESET_DELAY_US);
  return MSG_OK;
}

msg_t bmm350ReadCompensationData(const TagBmm350Device *dev)
{
  TagBmm350Compensation *comp;

  if (dev == NULL || dev->compensation == NULL)
    return MSG_RESET;
  comp = dev->compensation;
  comp->valid = false;

  for (uint8_t i = 0; i < BMM350_OTP_DATA_LENGTH; i++) {
    if (bmm350ReadOtpWord(dev, i, &comp->otp_data[i]) != MSG_OK)
      return MSG_RESET;
  }

  comp->variant_id = (uint8_t)((comp->otp_data[30] & 0x7F00U) >> 9);
  bmm350UpdateCompensation(comp);
  if (bmm350WriteU8(dev, BMM350_REG_OTP_CMD_REG,
                    BMM350_OTP_CMD_PWR_OFF_OTP) != MSG_OK)
    return MSG_RESET;

  comp->valid = true;
  return MSG_OK;
}

msg_t bmm350InitContinuous(const TagBmm350Device *dev,
                           bmm350_rate_t rate,
                           bmm350_performance_t performance)
{
  /*
   * The reset/read-trim/configure sequence deliberately happens as one public
   * init operation. Family code can recover a missed-sample run by calling this
   * helper again without knowing which BMM350 state was lost.
   */
  if (!bmm350CheckWhoami(dev))
    return MSG_RESET;
  if (bmm350Reset(dev) != MSG_OK)
    return MSG_RESET;
  if (!bmm350CheckWhoami(dev))
    return MSG_RESET;
  if (bmm350ReadCompensationData(dev) != MSG_OK)
    return MSG_RESET;
  if (bmm350ConfigureInterrupt(dev) != MSG_OK)
    return MSG_RESET;
  if (bmm350SetOdrPerformance(dev, rate, performance) != MSG_OK)
    return MSG_RESET;
  if (bmm350WriteU8(dev, BMM350_REG_PMU_CMD, BMM350_PMU_CMD_NM) != MSG_OK)
    return MSG_RESET;
  bmm350DelayUs(BMM350_SUSPEND_TO_NORMAL_DELAY_US);
  return MSG_OK;
}

msg_t bmm350InitPowerDown(const TagBmm350Device *dev)
{
  if (bmm350WriteU8(dev, BMM350_REG_PMU_CMD, BMM350_PMU_CMD_SUS) != MSG_OK)
    return MSG_RESET;
  bmm350DelayUs(BMM350_GOTO_SUSPEND_DELAY_US);
  return MSG_OK;
}

bool bmm350DataReady(const TagBmm350Device *dev)
{
  bool active_high;

  if (dev == NULL || !tagLineIsValid(dev->drdy))
    return false;

  active_high = dev->interrupt_polarity == BMM350_INT_ACTIVE_HIGH;
  return (palReadLine(dev->drdy) == PAL_HIGH) == active_high;
}

msg_t bmm350ReadRawSample(const TagBmm350Device *dev,
                          TagBmm350RawSample *sample)
{
  uint8_t data[BMM350_MAG_TEMP_DATA_LEN];
  uint32_t raw_x;
  uint32_t raw_y;
  uint32_t raw_z;
  uint32_t raw_t;
  uint8_t status;

  if (sample == NULL)
    return MSG_RESET;
  if (bmm350ReadRegister(dev, BMM350_REG_MAG_X_XLSB, data,
                         sizeof(data)) != MSG_OK)
    return MSG_RESET;

  raw_x = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
          ((uint32_t)data[2] << 16);
  raw_y = (uint32_t)data[3] | ((uint32_t)data[4] << 8) |
          ((uint32_t)data[5] << 16);
  raw_z = (uint32_t)data[6] | ((uint32_t)data[7] << 8) |
          ((uint32_t)data[8] << 16);
  raw_t = (uint32_t)data[9] | ((uint32_t)data[10] << 8) |
          ((uint32_t)data[11] << 16);

  sample->x = bmm350FixSign(raw_x, 24);
  sample->y = bmm350FixSign(raw_y, 24);
  sample->z = bmm350FixSign(raw_z, 24);
  sample->temperature = bmm350FixSign(raw_t, 24);

  /*
   * The normal readiness path uses the INT/DRDY pin, not status polling. For
   * latched INT mode, reading INT_STATUS after the coherent data burst clears
   * the latch so the pin can indicate the next sample.
   */
  (void)bmm350ReadU8(dev, BMM350_REG_INT_STATUS, &status);
  return MSG_OK;
}

msg_t bmm350ReadMagUT(const TagBmm350Device *dev,
                      float *mx, float *my, float *mz)
{
  TagBmm350RawSample raw;
  const TagBmm350Compensation *comp;
  float coeff[4];
  float out[4];
  float denom;
  float comp_x;
  float comp_y;
  float comp_z;

  if (dev == NULL || dev->compensation == NULL ||
      !dev->compensation->valid || mx == NULL || my == NULL || mz == NULL)
    return MSG_RESET;
  if (bmm350ReadRawSample(dev, &raw) != MSG_OK)
    return MSG_RESET;

  comp = dev->compensation;
  bmm350LsbToUTAndDegC(coeff);

  out[0] = (float)raw.x * coeff[0];
  out[1] = (float)raw.y * coeff[1];
  out[2] = (float)raw.z * coeff[2];
  out[3] = ((float)raw.temperature * coeff[3]) - 25.49f;

  out[3] = (1.0f + comp->sensitivity.t_sens) * out[3] +
           comp->offset.t_offs;

  out[0] *= 1.0f + comp->sensitivity.sens_x;
  out[1] *= 1.0f + comp->sensitivity.sens_y;
  out[2] *= 1.0f + comp->sensitivity.sens_z;

  out[0] += comp->offset.offset_x;
  out[1] += comp->offset.offset_y;
  out[2] += comp->offset.offset_z;

  out[0] += comp->tco.tco_x * (out[3] - comp->t0);
  out[1] += comp->tco.tco_y * (out[3] - comp->t0);
  out[2] += comp->tco.tco_z * (out[3] - comp->t0);

  out[0] /= 1.0f + comp->tcs.tcs_x * (out[3] - comp->t0);
  out[1] /= 1.0f + comp->tcs.tcs_y * (out[3] - comp->t0);
  out[2] /= 1.0f + comp->tcs.tcs_z * (out[3] - comp->t0);

  denom = 1.0f -
          comp->cross_axis.cross_y_x * comp->cross_axis.cross_x_y;
  if (denom == 0.0f)
    return MSG_RESET;

  comp_x = (out[0] - comp->cross_axis.cross_x_y * out[1]) / denom;
  comp_y = (out[1] - comp->cross_axis.cross_y_x * out[0]) / denom;
  comp_z = out[2] +
           (out[0] * (comp->cross_axis.cross_y_x *
                      comp->cross_axis.cross_z_y -
                      comp->cross_axis.cross_z_x) -
            out[1] * (comp->cross_axis.cross_z_y -
                      comp->cross_axis.cross_x_y *
                      comp->cross_axis.cross_z_x)) / denom;

  *mx = comp_x;
  *my = comp_y;
  *mz = comp_z;
  return MSG_OK;
}
