/**
 * @file ADXL367.c
 * @brief Descriptor-backed ADXL367 accelerometer register driver.
 * @author tag firmware authors
 * @date 2026-06-16
 */

#include "ADXL367.h"

#include "hal.h"

#include <stddef.h>
#include <stdint.h>

static uint8_t selected_range = 2;

void ADXL367_DeviceBegin(const TagAdxl367Device *device)
{
  tagBusPowerOn(&device->bus);
  tagBusBegin(&device->bus);
}

void ADXL367_DeviceEnd(const TagAdxl367Device *device)
{
  tagBusEnd(&device->bus);
  tagBusPowerOff(&device->bus);
}

static void adxl367Write(const TagAdxl367Device *device,
                         const uint8_t *buffer,
                         size_t length)
{
  const TagSpiDevice *spi = tagAdxl367SpiDevice(device);

  tagSpiSelect(spi);
  tagSpiWrite(spi, buffer, length);
  tagSpiDeselect(spi);
}

static void adxl367WriteThenRead(const TagAdxl367Device *device,
                                 const uint8_t *write_buffer,
                                 size_t write_length,
                                 uint8_t *read_buffer,
                                 size_t read_length)
{
  const TagSpiDevice *spi = tagAdxl367SpiDevice(device);

  tagSpiSelect(spi);
  tagSpiWrite(spi, write_buffer, write_length);
  tagSpiRead(spi, read_buffer, read_length);
  tagSpiDeselect(spi);
}

static int16_t adxl367SignExtend14(uint16_t value)
{
  value >>= 2;
  if (value & 0x2000U) {
    value |= 0xc000U;
  }
  return (int16_t)value;
}

static unsigned short adxl367PackHighFirst16(uint16_t value)
{
  return (unsigned short)(((value & 0x00ffU) << 8) | (value >> 8));
}

static void adxl367SetLeftAligned14Device(const TagAdxl367Device *device,
                                          uint16_t value,
                                          unsigned char high_register)
{
  uint16_t left_aligned = (uint16_t)((value & 0x3fffU) << 2);
  ADXL367_SetRegisterValueDevice(device,
                                 adxl367PackHighFirst16(left_aligned),
                                 high_register,
                                 2);
}

static void adxl367SetHighFirst16Device(const TagAdxl367Device *device,
                                        uint16_t value,
                                        unsigned char high_register)
{
  ADXL367_SetRegisterValueDevice(device,
                                 adxl367PackHighFirst16(value),
                                 high_register,
                                 2);
}

char ADXL367_InitDevice(const TagAdxl367Device *device)
{
  unsigned char regValue = 0;

  ADXL367_GetRegisterValueDevice(device, &regValue, ADXL367_REG_PARTID, 1);
  if (regValue != ADXL367_PART_ID) {
    return -1;
  }

  selected_range = 2;
  return 0;
}

void ADXL367_SetRegisterValueDevice(const TagAdxl367Device *device,
                                    unsigned short registerValue,
                                    unsigned char registerAddress,
                                    unsigned char bytesNumber)
{
  uint8_t buffer[4];

  if ((bytesNumber == 0) || (bytesNumber > 2)) {
    return;
  }

  buffer[0] = ADXL367_WRITE_REG;
  buffer[1] = registerAddress;
  buffer[2] = (uint8_t)(registerValue & 0x00ff);
  buffer[3] = (uint8_t)(registerValue >> 8);

  adxl367Write(device, buffer, bytesNumber + 2);
}

void ADXL367_GetRegisterValueDevice(const TagAdxl367Device *device,
                                    unsigned char *pReadData,
                                    unsigned char registerAddress,
                                    unsigned char bytesNumber)
{
  uint8_t buffer[2];

  if (bytesNumber == 0) {
    return;
  }

  buffer[0] = ADXL367_READ_REG;
  buffer[1] = registerAddress;

  adxl367WriteThenRead(device, buffer, sizeof(buffer), pReadData, bytesNumber);
}

void ADXL367_GetFifoValueDevice(const TagAdxl367Device *device,
                                unsigned char *pBuffer,
                                unsigned short bytesNumber)
{
  uint8_t command = ADXL367_READ_FIFO;

  if (bytesNumber == 0) {
    return;
  }

  adxl367WriteThenRead(device, &command, sizeof(command), pBuffer, bytesNumber);
}

void ADXL367_SoftwareResetDevice(const TagAdxl367Device *device)
{
  ADXL367_SetRegisterValueDevice(device, ADXL367_RESET_KEY,
                                 ADXL367_REG_SOFT_RESET, 1);
}

void ADXL367_DeinitDevice(const TagAdxl367Device *device)
{
  //const TagSpiDevice *spi = tagAdxl367SpiDevice(device);

  ADXL367_DeviceBegin(device);
  ADXL367_SetPowerModeDevice(device, ADXL367_MEASURE_STANDBY);
  //ADXL367_SoftwareResetDevice(device);
  //chThdSleepMilliseconds(2);
  ADXL367_DeviceEnd(device);

  //palSetLine(spi->cs);
  //palSetLineMode(spi->cs, PAL_MODE_OUTPUT_PUSHPULL);
  //palSetLineMode(spi->sck, PAL_MODE_INPUT_ANALOG);
  //palSetLineMode(spi->mosi, PAL_MODE_INPUT_ANALOG);
  //palSetLineMode(spi->miso, PAL_MODE_INPUT_ANALOG);
}

void ADXL367_SetPowerModeDevice(const TagAdxl367Device *device,
                                unsigned char pwrMode)
{
  unsigned char power_ctl = 0;

  ADXL367_GetRegisterValueDevice(device, &power_ctl, ADXL367_REG_POWER_CTL, 1);
  power_ctl &= (unsigned char)~ADXL367_POWER_CTL_MEASURE(0x3);
  if (pwrMode) {
    power_ctl |= ADXL367_POWER_CTL_MEASURE(ADXL367_MEASURE_ON);
  }
  ADXL367_SetRegisterValueDevice(device, power_ctl, ADXL367_REG_POWER_CTL, 1);
}

void ADXL367_SetRangeDevice(const TagAdxl367Device *device,
                            unsigned char gRange)
{
  unsigned char filter_ctl = 0;

  ADXL367_GetRegisterValueDevice(device, &filter_ctl, ADXL367_REG_FILTER_CTL, 1);
  filter_ctl &= (unsigned char)~ADXL367_FILTER_CTL_RANGE(0x3);
  filter_ctl |= ADXL367_FILTER_CTL_RANGE(gRange);
  ADXL367_SetRegisterValueDevice(device, filter_ctl, ADXL367_REG_FILTER_CTL, 1);
  selected_range = (uint8_t)((1U << gRange) * 2U);
}

void ADXL367_SetOutputRateDevice(const TagAdxl367Device *device,
                                 unsigned char outRate)
{
  unsigned char filter_ctl = 0;

  ADXL367_GetRegisterValueDevice(device, &filter_ctl, ADXL367_REG_FILTER_CTL, 1);
  filter_ctl &= (unsigned char)~ADXL367_FILTER_CTL_ODR(0x7);
  filter_ctl |= ADXL367_FILTER_CTL_ODR(outRate);
  ADXL367_SetRegisterValueDevice(device, filter_ctl, ADXL367_REG_FILTER_CTL, 1);
}

void ADXL367_GetXyzDevice(const TagAdxl367Device *device, short *x, short *y,
                          short *z)
{
  unsigned char xyzValues[6] = {0, 0, 0, 0, 0, 0};

  ADXL367_GetRegisterValueDevice(device, xyzValues, ADXL367_REG_XDATA_H, 6);
  *x = adxl367SignExtend14(((uint16_t)xyzValues[0] << 8) | xyzValues[1]);
  *y = adxl367SignExtend14(((uint16_t)xyzValues[2] << 8) | xyzValues[3]);
  *z = adxl367SignExtend14(((uint16_t)xyzValues[4] << 8) | xyzValues[5]);
}

#if defined(INCLUDE_FLOAT) && INCLUDE_FLOAT
void ADXL367_GetGxyzDevice(const TagAdxl367Device *device, float *x, float *y,
                           float *z)
{
  short raw_x = 0;
  short raw_y = 0;
  short raw_z = 0;
  const float scale = ((float)selected_range / 2.0f) * 0.00025f;

  ADXL367_GetXyzDevice(device, &raw_x, &raw_y, &raw_z);
  *x = (float)raw_x * scale;
  *y = (float)raw_y * scale;
  *z = (float)raw_z * scale;
}

float ADXL367_ReadTemperatureDevice(const TagAdxl367Device *device)
{
  unsigned char rawTempData[2] = {0, 0};
  short signedTemp = 0;

  ADXL367_GetRegisterValueDevice(device, rawTempData, ADXL367_REG_TEMP_H, 2);
  signedTemp = adxl367SignExtend14(
      ((uint16_t)rawTempData[0] << 8) | rawTempData[1]);

  return ((float)signedTemp + 1185.0f) / 54.0f;
}
#endif

void ADXL367_FifoSetupDevice(const TagAdxl367Device *device,
                             unsigned char mode,
                             unsigned short waterMarkLvl,
                             unsigned char enTempRead)
{
  unsigned char fifo_ctl =
      ADXL367_FIFO_CONTROL_FIFO_MODE(mode) |
      ADXL367_FIFO_CONTROL_FIFO_CHANNEL(
          enTempRead ? ADXL367_ALL_AXIS_TEMP : ADXL367_ALL_AXIS) |
      (((waterMarkLvl >> 8) & 0x01) ? ADXL367_FIFO_CONTROL_FIFO_SAMPLES : 0);

  ADXL367_SetRegisterValueDevice(device, fifo_ctl,
                                 ADXL367_REG_FIFO_CONTROL, 1);
  ADXL367_SetRegisterValueDevice(device, waterMarkLvl & 0x00ff,
                                 ADXL367_REG_FIFO_SAMPLES, 1);
}

void ADXL367_SetupActivityDetectionDevice(const TagAdxl367Device *device,
                                          unsigned char refOrAbs,
                                          unsigned short threshold,
                                          unsigned char time)
{
  unsigned char act_inact_ctl = 0;

  adxl367SetLeftAligned14Device(device, threshold, ADXL367_REG_THRESH_ACT_H);
  ADXL367_SetRegisterValueDevice(device, time, ADXL367_REG_TIME_ACT, 1);

  ADXL367_GetRegisterValueDevice(device, &act_inact_ctl,
                                 ADXL367_REG_ACT_INACT_CTL, 1);
  act_inact_ctl &= (unsigned char)~ADXL367_ACT_INACT_CTL_ACT_EN(0x3);
  act_inact_ctl |= ADXL367_ACT_INACT_CTL_ACT_EN(
      refOrAbs ? ADXL367_REFERENCED_ACTIVITY_ENABLE : ADXL367_ACTIVITY_ENABLE);
  ADXL367_SetRegisterValueDevice(device, act_inact_ctl,
                                 ADXL367_REG_ACT_INACT_CTL, 1);
}

void ADXL367_SetupInactivityDetectionDevice(const TagAdxl367Device *device,
                                            unsigned char refOrAbs,
                                            unsigned short threshold,
                                            unsigned short time)
{
  unsigned char act_inact_ctl = 0;

  adxl367SetLeftAligned14Device(device, threshold, ADXL367_REG_THRESH_INACT_H);
  adxl367SetHighFirst16Device(device, time, ADXL367_REG_TIME_INACT_H);

  ADXL367_GetRegisterValueDevice(device, &act_inact_ctl,
                                 ADXL367_REG_ACT_INACT_CTL, 1);
  act_inact_ctl &= (unsigned char)~ADXL367_ACT_INACT_CTL_INACT_EN(0x3);
  act_inact_ctl |= ADXL367_ACT_INACT_CTL_INACT_EN(
      refOrAbs ? ADXL367_REFERENCED_INACTIVITY_ENABLE
               : ADXL367_INACTIVITY_ENABLE);
  ADXL367_SetRegisterValueDevice(device, act_inact_ctl,
                                 ADXL367_REG_ACT_INACT_CTL, 1);
}
