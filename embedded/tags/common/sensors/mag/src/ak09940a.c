/**
 * @file ak09940a.c
 * @brief Descriptor-backed AK09940A magnetometer driver.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "ak09940a.h"
#include "power.h"
#include "timekeeping.h"

/** @name AK09940A device lifecycle
 * Helpers bracket magnetometer register access with descriptor-owned bus
 * power/session behavior.
 * @{
 */
/**
 * @brief Report whether the AK09940A descriptor has a switchable power rail.
 *
 * @param[in] device Magnetometer register descriptor.
 * @return true when the descriptor names a valid bus power line.
 */
static bool ak09940aHasPowerLine(const TagRegisterDevice *device)
{
  switch (device->bus.kind) {
  case TAG_BUS_SPI:
    return tagLineIsValid(tagBusSpiDevice(&device->bus)->pwr);
  case TAG_BUS_USART:
    return tagLineIsValid(tagBusUsartDevice(&device->bus)->pwr);
  case TAG_BUS_I2C:
    return tagLineIsValid(tagBusI2cDevice(&device->bus)->pwr);
  }
  return false;
}

/**
 * @brief Power and begin the bus session for an AK09940A device.
 *
 * @param[in] device Magnetometer register descriptor.
 */
void ak09940aDeviceBegin(const TagRegisterDevice *device)
{
  tagBusPowerOn(&device->bus);
  tagBusBegin(&device->bus);
}

/**
 * @brief End the bus session and power down an AK09940A device.
 *
 * @param[in] device Magnetometer register descriptor.
 */
void ak09940aDeviceEnd(const TagRegisterDevice *device)
{
  tagBusEnd(&device->bus);
  tagBusPowerOff(&device->bus);
}

/**
 * @brief Power the AK09940A, wait for rail settling, and begin its bus session.
 *
 * @param[in] device Magnetometer register descriptor.
 */
static void ak09940aPowerUpAndBeginBus(const TagRegisterDevice *device)
{
  tagBusPowerOn(&device->bus);
  if (ak09940aHasPowerLine(device))
    stopMilliseconds(1);
  tagBusBegin(&device->bus);
}
/** @} */

/** @name AK09940A register helpers
 * Register helpers adapt the generic TagRegisterDevice interface to the
 * AK09940A-specific register enum and MSG_OK/MSG_RESET convention.
 * @{
 */
/**
 * @brief Write one or more AK09940A registers.
 *
 * @param[in] device Magnetometer register descriptor.
 * @param[in] reg First register to write.
 * @param[in] val Source buffer.
 * @param[in] num Number of bytes to write.
 * @return MSG_OK on success or MSG_RESET on register-bus failure.
 */
static msg_t ak09940a_write_register(const TagRegisterDevice *device,
                                     enum AK09940A_Reg reg,
                                     const uint8_t *val, uint32_t num)
{
  if (tagRegisterWrite(device, (uint8_t)reg, val, num) != 0)
    return MSG_RESET;
  return MSG_OK;
}

/**
 * @brief Read one or more AK09940A registers.
 *
 * @param[in] device Magnetometer register descriptor.
 * @param[in] reg First register to read.
 * @param[out] val Destination buffer.
 * @param[in] num Number of bytes to read.
 * @return MSG_OK on success or MSG_RESET on register-bus failure.
 */
static msg_t ak09940a_read_register(const TagRegisterDevice *device,
                                    enum AK09940A_Reg reg,
                                    uint8_t *val, uint32_t num)
{
  if (tagRegisterRead(device, (uint8_t)reg, val, num) != 0)
    return MSG_RESET;
  return MSG_OK;
}

/**
 * @brief Convert public sample-rate selector to CNTL3 mode bits.
 *
 * @param[in] rate Public sample-rate selector.
 * @return CNTL3 mode bits for continuous sampling.
 */
static uint8_t ak09940a_rate_to_mode(ak09940_rate_t rate)
{
  switch (rate) {
  case AK09940_RATE_10HZ:
    return AK09940A_CNTL3_10HZ;
  case AK09940_RATE_20HZ:
    return AK09940A_CNTL3_20HZ;
  case AK09940_RATE_50HZ:
    return AK09940A_CNTL3_50HZ;
  case AK09940_RATE_100HZ:
    return AK09940A_CNTL3_100HZ;
  case AK09940_RATE_200HZ:
    return AK09940A_CNTL3_200HZ;
  case AK09940_RATE_400HZ:
    return AK09940A_CNTL3_400HZ;
  default:
    return AK09940A_CNTL3_10HZ;
  }
}

/**
 * @brief Pack drive/noise selector into CNTL1 and CNTL3 fields.
 *
 * @param[in] drive Public drive selector.
 * @param[out] cntl1 CNTL1 bits to apply.
 * @param[out] cntl3 CNTL3 bits to apply.
 */
static void ak09940a_pack_drive(ak09940_drive_t drive, uint8_t *cntl1,
                                uint8_t *cntl3)
{
  *cntl1 = 0;
  *cntl3 = 0;

  switch (drive) {
  case AK09940_DRIVE_LOW_POWER_1:
    *cntl3 = AK09940A_CNTL3_LP1;
    break;
  case AK09940_DRIVE_LOW_POWER_2:
    *cntl3 = AK09940A_CNTL3_LP2;
    break;
  case AK09940_DRIVE_LOW_NOISE_1:
    *cntl3 = AK09940A_CNTL3_LN1;
    break;
  case AK09940_DRIVE_LOW_NOISE_2:
    *cntl3 = AK09940A_CNTL3_LN2;
    break;
  case AK09940_DRIVE_ULTRA_LOW_POWER:
    *cntl1 = AK09940A_CNTL1_MT2_MSK;
    *cntl3 = AK09940A_CNTL3_LP1;
    break;
  default:
    *cntl3 = AK09940A_CNTL3_LP1;
    break;
  }
}

/**
 * @brief Sign-extend one AK09940A 18-bit magnetic sample.
 *
 * @param[in] l Low byte.
 * @param[in] m Middle byte.
 * @param[in] h High byte containing sign and upper data bits.
 * @return Signed 18-bit sample in a 32-bit integer.
 */
static int32_t ak09940a_convert_18bit(uint8_t l, uint8_t m, uint8_t h)
{
  int32_t raw = ((int32_t)(h & 0x03U) << 16) | ((int32_t)m << 8) | l;
  if ((raw & 0x20000) != 0)
    raw |= (int32_t)0xFFFC0000;
  return raw;
}
/** @} */

/** @name AK09940A register-descriptor-backed API
 * Explicit register API used by tag/family magnetometer code.
 * @{
 */
/**
 * @brief Optionally trigger and read one raw sample payload.
 *
 * @param[in] device Magnetometer register descriptor.
 * @param[in] single true to request a single measurement before polling.
 * @param[out] xyz Ten-byte raw sample payload.
 * @return true when a fresh sample was read.
 */
bool ak09940aSample(const TagRegisterDevice *device, bool single, uint8_t *xyz)
{
  uint8_t raw[11];
  uint8_t status = 0;

  if (single) {
    uint8_t command = AK09940A_CNTL3_SINGLE_MEASURE | AK09940A_CNTL3_LN2;
    if (ak09940a_write_register(device, AK09940A_CNTL3, &command, 1) != MSG_OK)
      return false;
  }

  for (int i = 0; i < 2; i++) {
    if (ak09940a_read_register(device, AK09940A_ST, &status, 1) != MSG_OK)
      return false;
    if ((status & AK09940A_ST_DRDY_MSK) != 0) {
      if (ak09940a_read_register(device, AK09940A_HXL, raw,
                                 sizeof(raw)) != MSG_OK)
        return false;
      memcpy(xyz, raw, 10);
      return true;
    }
    stopMilliseconds(4);
  }
  return false;
}

/**
 * @brief Initialize AK09940A for the historical sample mode API.
 *
 * @param[in] device Magnetometer register descriptor.
 * @param[in] mode Historical mode selector.
 */
void ak09940aInit(const TagRegisterDevice *device, ak09940_mode_t mode)
{
  uint8_t command = ((uint8_t)mode) | AK09940A_CNTL3_LN2;
  ak09940aPowerUpAndBeginBus(device);
  if (mode > AK09940A_CNTL3_SINGLE_MEASURE)
    (void)ak09940a_write_register(device, AK09940A_CNTL3, &command, 1);
}

/**
 * @brief Check AK09940A identity using the descriptor binding.
 *
 * @param[in] device Magnetometer register descriptor.
 * @return true when WHOAMI matches expected values.
 */
bool ak09940aTest(const TagRegisterDevice *device)
{
  bool ok;
  ak09940aPowerUpAndBeginBus(device);
  ok = ak09940aCheckWhoami(device);
  ak09940aDeviceEnd(device);
  return ok;
}

/**
 * @brief Check AK09940A WHOAMI registers.
 *
 * @param[in] device Magnetometer register descriptor.
 * @return true when company and product IDs match.
 */
bool ak09940aCheckWhoami(const TagRegisterDevice *device)
{
  uint8_t wia[2];
  if (ak09940a_read_register(device, AK09940A_WIA1, wia, 2) != MSG_OK)
    return false;
  return ((wia[0] == AK09940A_COMPANY_ID) &&
          (wia[1] == AK09940A_PRODUCT_ID));
}

/**
 * @brief Soft-reset AK09940A register state.
 *
 * @param[in] device Magnetometer register descriptor.
 * @return MSG_OK on success.
 */
msg_t ak09940aReset(const TagRegisterDevice *device)
{
  uint8_t cntl4 = AK09940A_CNTL4_SRST;
  msg_t rc = ak09940a_write_register(device, AK09940A_CNTL4, &cntl4, 1);

  if (rc == MSG_OK)
    stopMilliseconds(1);
  return rc;
}

/**
 * @brief Put AK09940A into power-down mode.
 *
 * @param[in] device Magnetometer register descriptor.
 * @return MSG_OK on success.
 */
msg_t ak09940aInitPowerDown(const TagRegisterDevice *device)
{
  uint8_t cntl3 = AK09940A_CNTL3_PWRDOWN;
  return ak09940a_write_register(device, AK09940A_CNTL3, &cntl3, 1);
}

/**
 * @brief Configure continuous magnetic sampling.
 *
 * @param[in] device Magnetometer register descriptor.
 * @param[in] rate Sample-rate selector.
 * @param[in] drive Drive/noise selector.
 * @param[in] temp_mode Temperature-channel mode.
 * @return MSG_OK on success.
 */
msg_t ak09940aInitContinuous(const TagRegisterDevice *device,
                             ak09940_rate_t rate,
                             ak09940_drive_t drive,
                             ak09940_temp_mode_t temp_mode)
{
  uint8_t cntl1;
  uint8_t cntl2;
  uint8_t cntl3;

  ak09940a_pack_drive(drive, &cntl1, &cntl3);
  cntl2 = (temp_mode == AK09940_TEMP_ENABLED) ? AK09940A_CNTL2_TEM_MSK : 0;
  cntl3 |= ak09940a_rate_to_mode(rate);

  if (ak09940aInitPowerDown(device) != MSG_OK)
    return MSG_RESET;
  stopMilliseconds(1);
  if (ak09940a_write_register(device, AK09940A_CNTL1, &cntl1, 1) != MSG_OK)
    return MSG_RESET;
  if (ak09940a_write_register(device, AK09940A_CNTL2, &cntl2, 1) != MSG_OK)
    return MSG_RESET;
  return ak09940a_write_register(device, AK09940A_CNTL3, &cntl3, 1);
}

/**
 * @brief Configure externally triggered magnetic sampling.
 *
 * @param[in] device Magnetometer register descriptor.
 * @param[in] drive Drive/noise selector.
 * @param[in] temp_mode Temperature-channel mode.
 * @return MSG_OK on success.
 */
msg_t ak09940aInitTriggered(const TagRegisterDevice *device,
                            ak09940_drive_t drive,
                            ak09940_temp_mode_t temp_mode)
{
  uint8_t cntl1;
  uint8_t cntl2;
  uint8_t cntl3;

  ak09940a_pack_drive(drive, &cntl1, &cntl3);
  cntl1 |= AK09940A_CNTL1_DTSET_MSK;
  cntl2 = (temp_mode == AK09940_TEMP_ENABLED) ? AK09940A_CNTL2_TEM_MSK : 0;
  cntl3 |= AK09940A_CNTL3_EXTERNAL_TRIGGER;

  if (ak09940a_write_register(device, AK09940A_CNTL1, &cntl1, 1) != MSG_OK)
    return MSG_RESET;
  if (ak09940a_write_register(device, AK09940A_CNTL2, &cntl2, 1) != MSG_OK)
    return MSG_RESET;
  return ak09940a_write_register(device, AK09940A_CNTL3, &cntl3, 1);
}

/**
 * @brief Report whether a magnetic sample is ready.
 *
 * @param[in] device Magnetometer register descriptor.
 * @param[in] is_continuous true when using continuous-mode status semantics.
 * @return true when a sample is ready.
 */
bool ak09940aDataReady(const TagRegisterDevice *device, bool is_continuous)
{
  uint8_t st1;

  (void)is_continuous;

  if (ak09940a_read_register(device, AK09940A_ST1, &st1, 1) != MSG_OK)
    return false;
  return ((st1 & AK09940A_ST_DRDY_MSK) != 0);
}

/**
 * @brief Read and decode one AK09940A magnetic sample.
 *
 * @param[in] device Magnetometer register descriptor.
 * @param[out] mx_raw Raw signed X-axis sample.
 * @param[out] my_raw Raw signed Y-axis sample.
 * @param[out] mz_raw Raw signed Z-axis sample.
 * @param[out] temp_raw Raw signed temperature sample.
 * @return true when a valid sample was decoded.
 */
bool ak09940aReadSample(const TagRegisterDevice *device, int32_t *mx_raw,
                        int32_t *my_raw, int32_t *mz_raw, int16_t *temp_raw)
{
  uint8_t st1;
  uint8_t buf[11];

  if (ak09940a_read_register(device, AK09940A_ST1, &st1, 1) != MSG_OK)
    return false;
  if ((st1 & AK09940A_ST_DRDY_MSK) == 0)
    return false;

  if (ak09940a_read_register(device, AK09940A_HXL, buf, sizeof(buf)) != MSG_OK)
    return false;

  if ((buf[10] & (AK09940A_ST2_INV_MSK | AK09940A_ST2_DOR_MSK)) != 0)
    return false;

  *mx_raw = ak09940a_convert_18bit(buf[0], buf[1], buf[2]);
  *my_raw = ak09940a_convert_18bit(buf[3], buf[4], buf[5]);
  *mz_raw = ak09940a_convert_18bit(buf[6], buf[7], buf[8]);
  *temp_raw = (int16_t)(int8_t)buf[9];
  return true;
}

/**
 * @brief Run AK09940A self-test mode and range-check the result.
 *
 * @param[in] device Magnetometer register descriptor.
 * @return true when the self-test sample is in the expected range.
 */
bool ak09940aSelfTest(const TagRegisterDevice *device)
{
  uint8_t cntl1;
  uint8_t cntl2;
  uint8_t cntl3;
  int32_t mx;
  int32_t my;
  int32_t mz;
  int16_t temp;

  ak09940a_pack_drive(AK09940_DRIVE_LOW_NOISE_2, &cntl1, &cntl3);
  cntl2 = AK09940A_CNTL2_TEM_MSK;
  cntl3 |= AK09940A_CNTL3_SELF_TEST_MODE;

  if (ak09940a_write_register(device, AK09940A_CNTL1, &cntl1, 1) != MSG_OK)
    return false;
  if (ak09940a_write_register(device, AK09940A_CNTL2, &cntl2, 1) != MSG_OK)
    return false;
  if (ak09940a_write_register(device, AK09940A_CNTL3, &cntl3, 1) != MSG_OK)
    return false;

  stopMilliseconds(10);
  if (!ak09940aReadSample(device, &mx, &my, &mz, &temp))
    return false;
  if (mx < -20000 || mx > 20000)
    return false;
  if (my < -20000 || my > 20000)
    return false;
  if (mz < -20000 || mz > 20000)
    return false;
  return true;
}
/** @} */

/** @name AK09940A conversion helpers
 * Convert raw magnetic samples into physical units.
 * @{
 */
/**
 * @brief Convert raw magnetic samples to microtesla.
 *
 * @param[in] mx_raw Raw X-axis sample.
 * @param[in] my_raw Raw Y-axis sample.
 * @param[in] mz_raw Raw Z-axis sample.
 * @param[out] mx_uT X-axis field in microtesla.
 * @param[out] my_uT Y-axis field in microtesla.
 * @param[out] mz_uT Z-axis field in microtesla.
 */
void ak09940_convert_to_uT(int32_t mx_raw, int32_t my_raw, int32_t mz_raw,
                           float *mx_uT, float *my_uT, float *mz_uT)
{
  if (mx_uT)
    *mx_uT = mx_raw * AK09940A_SENSITIVITY_UT_PER_LSB;
  if (my_uT)
    *my_uT = my_raw * AK09940A_SENSITIVITY_UT_PER_LSB;
  if (mz_uT)
    *mz_uT = mz_raw * AK09940A_SENSITIVITY_UT_PER_LSB;
}

/**
 * @brief Convert raw magnetic samples to nanotesla.
 *
 * @param[in] mx_raw Raw X-axis sample.
 * @param[in] my_raw Raw Y-axis sample.
 * @param[in] mz_raw Raw Z-axis sample.
 * @param[out] mx_nT X-axis field in nanotesla.
 * @param[out] my_nT Y-axis field in nanotesla.
 * @param[out] mz_nT Z-axis field in nanotesla.
 */
void ak09940_convert_to_nT(int32_t mx_raw, int32_t my_raw, int32_t mz_raw,
                           int32_t *mx_nT, int32_t *my_nT, int32_t *mz_nT)
{
  if (mx_nT)
    *mx_nT = mx_raw * AK09940A_SENSITIVITY_NT_PER_LSB;
  if (my_nT)
    *my_nT = my_raw * AK09940A_SENSITIVITY_NT_PER_LSB;
  if (mz_nT)
    *mz_nT = mz_raw * AK09940A_SENSITIVITY_NT_PER_LSB;
}
/** @} */
