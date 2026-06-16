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

#define LOOP_MODE(active, inactive) (ADXL367_MODE_LOOP << 4| inactive << 2| active)
#define REFERENCED_LOOP_MODE LOOP_MODE(ADXL367_REFERENCED_ACTIVITY_ENABLE, ADXL367_REFERENCED_INACTIVITY_ENABLE)
#define ABSOLUTE_LOOP_MODE LOOP_MODE(ADXL367_ACTIVITY_ENABLE, ADXL367_INACTIVITY_ENABLE)
#define MIXED_LOOP_MODE LOOP_MODE(ADXL367_REFERENCED_ACTIVITY_ENABLE, ADXL367_INACTIVITY_ENABLE)

#define ADXL_SAMPLE_RATE ADXL367_ODR_12P5HZ
#define UINT16SWAP(x) (((x&0xff)<<8) | ((x>>8)&0xff))

/*

The ADXL367 has an unfortunate quirk in how activity is detected at startup.  This is from the data sheet:

When using loop mode, it is important to note that the AWAKE bit is
always asserted when the device first enters measurement mode.
The device is currently waiting for an activity event. Therefore,
this AWAKE bit remains asserted until activity is detected and an
inactivity event is detected. To avoid this, the device must enter
measurement mode with an activity threshold below the noise
level of the ADXL367 and an inactivity threshold greater than 1
g. This allows the device to deassert the AWAKE bit immediately
after entering measurement mode. The activity threshold can then
be raised to the desired level (while still in measurement mode).

*/

bool initActivitySensor(void)
{
  static const unsigned int RANGE_SHIFT = 2+2; // 2 bits for range, 2 bits for reserved bits in the register
  unsigned int act_thresh = (sconfig.adxl_act_thresh_cnt + 1100) << RANGE_SHIFT; // add 1000mg to ensure the device starts up in the correct state

  debug_log_printf("Initializing ADXL367\r\n");

  ADXL367_DeviceBegin(TAG_ACCEL_DEVICE);
  ADXL367_SoftwareResetDevice(TAG_ACCEL_DEVICE);
  stopMilliseconds(20);


  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, ADXL_SAMPLE_RATE,
                                 ADXL367_REG_FILTER_CTL, 1);

  // We have to follow data sheet directions here

  // 1. Set activity threshold to 25 mg

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, UINT16SWAP(25<<RANGE_SHIFT),
                                 ADXL367_REG_THRESH_ACT_H, 2);

  // 2. Set activity timer to zero

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, 0,
                                 ADXL367_REG_TIME_ACT, 1);

  // 3. Set inactivity threshold to 1100 mg

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, UINT16SWAP(1100<<RANGE_SHIFT),
                                 ADXL367_REG_THRESH_INACT_H, 2);

  // 4. Set inactivity timer to 0

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, 0,
                                 ADXL367_REG_TIME_INACT_H, 2);

  // 5. Enable activity and inactivity detection in loop mode with referenced data

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, ABSOLUTE_LOOP_MODE,
                                 ADXL367_REG_ACT_INACT_CTL, 1);


  // 6. Other settings

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, ADXL367_OP_MEASURE,
                                 ADXL367_REG_POWER_CTL, 1);

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, ADXL367_INTMAP1_AWAKE,
                                 ADXL367_REG_INTMAP1_LWR, 1);

  // Timer control register: 6 samples/second.
  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, 1<<6,
                                 ADXL367_REG_TIMER_CTL, 1);


  // Start the device in measurement mode  with auto-sleep and wakeup enabled.

  ADXL367_SetRegisterValueDevice(
      TAG_ACCEL_DEVICE,
      ADXL367_POWER_CTL_WAKEUP |
          ADXL367_POWER_CTL_AUTOSLEEP |
          ADXL367_OP_MEASURE,
      ADXL367_REG_POWER_CTL,
      1);

  // wait for the accel int line to go low
/*
  for (int i = 0; i < 15; i++){
    if (!palReadLine(LINE_ACCEL_INT)) {
      debug_log_printf("AWAKE  low detected\r\n");
      break;
    }
    stopMilliseconds(20);
  }
    */

  // reconfigure

  
  // set inactive samples

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE,
                                 UINT16SWAP(sconfig.adxl_inactive_samples),
                                 ADXL367_REG_TIME_INACT_H, 2);

  // set active threshold

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE,
                                 UINT16SWAP(act_thresh),
                                 ADXL367_REG_THRESH_ACT_H, 2);

  // set loop mode to absolute

  //ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, ABSOLUTE_LOOP_MODE,
  //                               ADXL367_REG_ACT_INACT_CTL, 1);

  ADXL367_DeviceEnd(TAG_ACCEL_DEVICE);
  return true;
}
