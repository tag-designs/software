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
#include "app.h"

#define DEFAULT_MODE(active, inactive) ((inactive << 2) | active)
#define ABSOLUTE_DEFAULT_MODE                                                \
  DEFAULT_MODE(ADXL367_ACTIVITY_ENABLE, ADXL367_INACTIVITY_ENABLE)

#define ADXL_SAMPLE_RATE ADXL367_ODR_12P5HZ
#define UINT16SWAP(x) (((x&0xff)<<8) | ((x>>8)&0xff))

static bool activity_awake = false;
static bool activity_wake_line_serviced = false;

typedef enum {
  ADXL367_ARMED_ACT,
  ADXL367_ARMED_INACT,
} Adxl367ArmedInterrupt;

static Adxl367ArmedInterrupt activity_armed_interrupt = ADXL367_ARMED_ACT;

static const char *armedInterruptName(void)
{
  return (activity_armed_interrupt == ADXL367_ARMED_INACT) ? "inact" : "act";
}

static void armActivityInterrupt(void)
{
  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, ADXL367_INTMAP1_ACT,
                                 ADXL367_REG_INTMAP1_LWR, 1);
  activity_armed_interrupt = ADXL367_ARMED_ACT;
}

static void armInactivityInterrupt(void)
{
  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, ADXL367_INTMAP1_INACT,
                                 ADXL367_REG_INTMAP1_LWR, 1);
  activity_armed_interrupt = ADXL367_ARMED_INACT;
}

static unsigned char readActivityStatus(void)
{
  unsigned char status = 0;
  ADXL367_GetRegisterValueDevice(TAG_ACCEL_DEVICE, &status,
                                 ADXL367_REG_STATUS, 1);
  return status;
}

static bool statusIndicatesAwake(unsigned char status)
{
  bool was_awake = activity_awake;

  if (activity_awake) {
    if (status & ADXL367_STATUS_INACT) {
      activity_awake = false;
    }
  } else {
    if (status & ADXL367_STATUS_ACT) {
      activity_awake = true;
    }
  }
  debug_log_printf("ADXL367 status 0x%02x armed %s awake %u->%u act %u inact %u\r\n",
                   status,
                   armedInterruptName(),
                   was_awake ? 1 : 0,
                   activity_awake ? 1 : 0,
                   (status & ADXL367_STATUS_ACT) ? 1 : 0,
                   (status & ADXL367_STATUS_INACT) ? 1 : 0);
  return activity_awake;
}

/*
   Use the default sample rate - 12.5Hz and default range of 2G
*/

bool initActivitySensor(void)
{
  static const unsigned int RANGE_SHIFT = 2+2; // 2 bits for range, 2 bits for reserved bits in the register
  unsigned int act_thresh = sconfig.adxl_act_thresh_cnt << RANGE_SHIFT;

  debug_log_printf("Initializing ADXL367 with activity threshold %u mg\r\n",
                    act_thresh >> RANGE_SHIFT);
  debug_log_printf("Initializing ADXL367\r\n");

  ADXL367_DeinitDevice(TAG_ACCEL_DEVICE);

  ADXL367_DeviceBegin(TAG_ACCEL_DEVICE);
  ADXL367_SetPowerModeDevice(TAG_ACCEL_DEVICE, ADXL367_MEASURE_STANDBY);

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, ADXL_SAMPLE_RATE,
                                 ADXL367_REG_FILTER_CTL, 1);

  // Set the configured activity threshold.

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, UINT16SWAP(act_thresh),
                                 ADXL367_REG_THRESH_ACT_H, 2);

  // Set inactivity threshold to 1100 mg

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, UINT16SWAP(1100<<RANGE_SHIFT),
                                 ADXL367_REG_THRESH_INACT_H, 2);

  // Enable independent activity and inactivity detection in default mode.

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, ABSOLUTE_DEFAULT_MODE,
                                 ADXL367_REG_ACT_INACT_CTL, 1);


  // Timer control register: 6 samples/second.

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, 1<<6,
                                 ADXL367_REG_TIMER_CTL, 1);

  // Set inactivity timer.

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE,
                                 UINT16SWAP(sconfig.adxl_inactive_samples),
                                 ADXL367_REG_TIME_INACT_H, 2);

  // Start measurement mode with wakeup enabled

  ADXL367_SetRegisterValueDevice(
      TAG_ACCEL_DEVICE,
      ADXL367_POWER_CTL_WAKEUP |
          ADXL367_POWER_CTL_MEASURE(ADXL367_MEASURE_ON),
      ADXL367_REG_POWER_CTL, 1);

  activity_awake = false;
  activity_wake_line_serviced = false;
  (void)readActivityStatus();
  armActivityInterrupt();
  (void)readActivityStatus();

  ADXL367_DeviceEnd(TAG_ACCEL_DEVICE);
  return true;
}

bool checkActivitySensorAwake(bool *is_awake)
{
  if (is_awake == NULL)
    return false;

  if (!palReadLine(LINE_ACCEL_INT)) {
    activity_wake_line_serviced = false;
    *is_awake = activity_awake;
    return true;
  }

  if (activity_wake_line_serviced) {
    *is_awake = activity_awake;
    return true;
  }

  activity_wake_line_serviced = true;

  ADXL367_DeviceBegin(TAG_ACCEL_DEVICE);

  unsigned char status = readActivityStatus();
  bool awake = statusIndicatesAwake(status);

  if (awake) {
    armInactivityInterrupt();
  } else {
    armActivityInterrupt();
  }

  (void)readActivityStatus();
  if (!palReadLine(LINE_ACCEL_INT)) {
    activity_wake_line_serviced = false;
  }
  ADXL367_DeviceEnd(TAG_ACCEL_DEVICE);

  *is_awake = awake;
  return true;
}
