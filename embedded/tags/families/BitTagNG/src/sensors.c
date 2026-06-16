/**
 * @file sensors.c
 * @brief BitTagNG sensor setup.
 * @author tag firmware authors
 * @date 2026-06-16
 */

#include "ADXL367.h"
#include "tag.pb.h"
#include "config.h"
#include "devices.h"
#include "hal.h"
#include "sensors.h"
#include "timekeeping.h"

#define ADXL_SAMPLE_RATE ADXL367_ODR_12P5HZ
#define UINT16SWAP(x) (((x&0xff)<<8) | ((x>>8)&0xff))

bool initActivitySensor(void)
{
  ADXL367_DeviceBegin(TAG_ACCEL_DEVICE);
  ADXL367_SoftwareResetDevice(TAG_ACCEL_DEVICE);
  stopMilliseconds(20);

  // Start measurement so the AWAKE interrupt can settle before final config.
  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, ADXL367_OP_MEASURE,
                                 ADXL367_REG_POWER_CTL, 1);

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, ADXL367_INTMAP1_AWAKE,
                                 ADXL367_REG_INTMAP1_LWR, 1);

  // Timer control register: 6 samples/second.
  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, 1<<6,
                                 ADXL367_REG_TIMER_CTL, 1);

  // Inactivity threshold maximum: 4400 counts = 1100mg at 2g range.
  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, UINT16SWAP(4400<<2),
                                 ADXL367_REG_THRESH_INACT_H, 2);

  // Activity threshold default: 250mg.
  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, UINT16SWAP(1000),
                                 ADXL367_REG_THRESH_ACT_H, 2);

  // Referenced loop mode with activity and inactivity detection enabled.
  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, 0x3F,
                                 ADXL367_REG_ACT_INACT_CTL, 1);

  ADXL367_SetRegisterValueDevice(
      TAG_ACCEL_DEVICE,
      ADXL367_POWER_CTL_WAKEUP | ADXL367_POWER_CTL_AUTOSLEEP |
          ADXL367_OP_MEASURE,
      ADXL367_REG_POWER_CTL,
      1);

  while (palReadLine(LINE_ACCEL_INT)) {
    stopMilliseconds(20);
  }

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, ADXL_SAMPLE_RATE,
                                 ADXL367_REG_FILTER_CTL, 1);

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE,
                                 UINT16SWAP(sconfig.adxl_inactive_samples),
                                 ADXL367_REG_TIME_INACT_H, 2);

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE,
                                 UINT16SWAP((sconfig.adxl_act_thresh_cnt)<<2),
                                 ADXL367_REG_THRESH_ACT_H, 2);

  ADXL367_DeviceEnd(TAG_ACCEL_DEVICE);
  return true;
}
