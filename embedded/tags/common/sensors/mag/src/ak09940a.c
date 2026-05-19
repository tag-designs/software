#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "ak09940a.h"

void ak09940aDeviceBegin(const TagMagDevice *device)
{
  if (device->power_on)
    device->power_on();
  if (device->bus_begin)
    device->bus_begin();
}

void ak09940aDeviceEnd(const TagMagDevice *device)
{
  if (device->bus_end)
    device->bus_end();
  if (device->power_off)
    device->power_off();
}

static void ak09940aPowerUpAndBeginBus(const TagMagDevice *device)
{
  if (device->power_on)
    device->power_on();
  if (device->sleep_ms)
    device->sleep_ms(1);
  if (device->bus_begin)
    device->bus_begin();
}

static msg_t ak09940a_write_register(const TagMagDevice *device,
                                     enum AK09940A_Reg reg,
                                     const uint8_t *val, uint32_t num)
{
  if (tagRegisterWrite(device->registers, (uint8_t)reg, val, num) != 0)
    return MSG_RESET;
  return MSG_OK;
}

static msg_t ak09940a_read_register(const TagMagDevice *device,
                                    enum AK09940A_Reg reg,
                                    uint8_t *val, uint32_t num)
{
  if (tagRegisterRead(device->registers, (uint8_t)reg, val, num) != 0)
    return MSG_RESET;
  return MSG_OK;
}

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

static int32_t ak09940a_convert_18bit(uint8_t l, uint8_t m, uint8_t h)
{
  int32_t raw = ((int32_t)(h & 0x03U) << 16) | ((int32_t)m << 8) | l;
  if ((raw & 0x20000) != 0)
    raw |= (int32_t)0xFFFC0000;
  return raw;
}

bool ak09940aSample(const TagMagDevice *device, bool single, uint8_t *xyz)
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
    device->sleep_ms(4);
  }
  return false;
}

void ak09940aInit(const TagMagDevice *device, ak09940_mode_t mode)
{
  uint8_t command = ((uint8_t)mode) | AK09940A_CNTL3_LN2;
  ak09940aPowerUpAndBeginBus(device);
  if (mode > AK09940A_CNTL3_SINGLE_MEASURE)
    (void)ak09940a_write_register(device, AK09940A_CNTL3, &command, 1);
}

bool ak09940aTest(const TagMagDevice *device)
{
  bool ok;
  ak09940aPowerUpAndBeginBus(device);
  ok = ak09940aCheckWhoami(device);
  ak09940aDeviceEnd(device);
  return ok;
}

bool ak09940aCheckWhoami(const TagMagDevice *device)
{
  uint8_t wia[2];
  if (ak09940a_read_register(device, AK09940A_WIA1, wia, 2) != MSG_OK)
    return false;
  return ((wia[0] == AK09940A_COMPANY_ID) &&
          (wia[1] == AK09940A_PRODUCT_ID));
}

msg_t ak09940aInitPowerDown(const TagMagDevice *device)
{
  uint8_t cntl3 = AK09940A_CNTL3_PWRDOWN;
  return ak09940a_write_register(device, AK09940A_CNTL3, &cntl3, 1);
}

msg_t ak09940aInitContinuous(const TagMagDevice *device, ak09940_rate_t rate,
                             ak09940_drive_t drive,
                             ak09940_temp_mode_t temp_mode)
{
  uint8_t cntl1;
  uint8_t cntl2;
  uint8_t cntl3;

  if (device->set_trigger_output)
    device->set_trigger_output(false);

  ak09940a_pack_drive(drive, &cntl1, &cntl3);
  cntl2 = (temp_mode == AK09940_TEMP_ENABLED) ? AK09940A_CNTL2_TEM_MSK : 0;
  cntl3 |= ak09940a_rate_to_mode(rate);

  if (ak09940a_write_register(device, AK09940A_CNTL1, &cntl1, 1) != MSG_OK)
    return MSG_RESET;
  if (ak09940a_write_register(device, AK09940A_CNTL2, &cntl2, 1) != MSG_OK)
    return MSG_RESET;
  return ak09940a_write_register(device, AK09940A_CNTL3, &cntl3, 1);
}

msg_t ak09940aInitTriggered(const TagMagDevice *device, ak09940_drive_t drive,
                            ak09940_temp_mode_t temp_mode)
{
  uint8_t cntl1;
  uint8_t cntl2;
  uint8_t cntl3;

  if (device->set_trigger_output)
    device->set_trigger_output(true);

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

msg_t ak09940aTrigger(const TagMagDevice *device)
{
  if (!device->trigger)
    return MSG_RESET;
  device->trigger();
  return MSG_OK;
}

bool ak09940aDataReady(const TagMagDevice *device, bool is_continuous)
{
  uint8_t st1;

  if (is_continuous && device->data_ready_line)
    return device->data_ready_line();

  if (ak09940a_read_register(device, AK09940A_ST1, &st1, 1) != MSG_OK)
    return false;
  return ((st1 & AK09940A_ST_DRDY_MSK) != 0);
}

bool ak09940aReadSample(const TagMagDevice *device, int32_t *mx_raw,
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

bool ak09940aSelfTest(const TagMagDevice *device)
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

  device->sleep_ms(10);
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
