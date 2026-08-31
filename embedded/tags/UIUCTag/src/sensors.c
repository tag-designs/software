/**
 * @file sensors.c
 * @brief UIUCTag collection-sensor configuration and sampling.
 * @author tag firmware authors
 * @date 2026-08-31
 *
 * @details Implements the sensor half of UIUCTag acquisition: ADXL367 wake-mode
 *          activity detection and BMP585 forced-mode pressure sampling. The
 *          RUNNING state handler in state_run.c uses only the sensors.h
 *          interface, which is what keeps the eventual LPS_RDY interrupt work
 *          confined to this file.
 *
 * @note    UIUCTag binds the ADXL367 to USART2 in synchronous 4-wire SPI mode
 *          and the BMP585 to SPI1; both bindings live in the tag-local
 *          devices.c descriptors, so this file is transport-agnostic.
 *
 * @see     ../../families/BitPresTag/design/uiuctag-data-collection.md
 */

#include "hal.h"
#include <stdbool.h>

#include "tag.pb.h"
#include "config.h"
#include "devices.h"
#include "sensors.h"

#include "ADXL367.h"
#include "bmp581.h"

/** Output data rate used for the forced-mode pressure conversion. */
#define UIUCTAG_PRESSURE_ODR BMP581_ODR_50HZ
/** DRDY poll budget for one forced conversion, in microseconds. */
#define UIUCTAG_PRESSURE_TIMEOUT_US 100000U

/**
 * @brief Return the quiet-NaN sentinel used for missing sensor samples.
 *
 * @details The log stores pressure and temperature as floats, and erased
 *          external flash reads back as all-ones — itself a quiet NaN. Writing
 *          NaN for a failed conversion therefore gives host loaders a single
 *          rule, "NaN means no measurement", that covers both a slot that was
 *          never written and a sample the sensor refused to produce.
 *
 * @return Quiet NaN.
 */
static inline float missing_sample(void)
{
  return __builtin_nanf("");
}

/* Public API contract documented in sensors.h. */
void initDataCollection(void)
{
  ADXL367_DeviceBegin(TAG_ACCEL_DEVICE);
  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, 0, ADXL367_REG_POWER_CTL,
                                 1);

  // set adxl filter;

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE,
                                 sconfig.adxl_filter_range_rate,
                                 ADXL367_REG_FILTER_CTL, 1);
  // set adxl activity detection

  ADXL367_SetupActivityDetectionDevice(TAG_ACCEL_DEVICE, 1,
                                       sconfig.adxl_act_thresh_cnt, 2);

  // set adxl inactivity detection

  ADXL367_SetupInactivityDetectionDevice(TAG_ACCEL_DEVICE, 1,
                                         sconfig.adxl_inact_thresh_cnt,
                                         sconfig.adxl_inactive_samples);

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, 0x3F,
                                 ADXL367_REG_ACT_INACT_CTL, 1);

  // interrupt -- caused by AWAKE going active
  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, ADXL367_INTMAP2_AWAKE,
                                 ADXL367_REG_INTMAP2_LWR, 1);
  // power
  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE,
                                 ADXL367_POWER_CTL_MEASURE(ADXL367_MEASURE_ON) |
                                 ADXL367_POWER_CTL_WAKEUP,
                                 ADXL367_REG_POWER_CTL, 1);
  ADXL367_DeviceEnd(TAG_ACCEL_DEVICE);
}

/* Public API contract documented in sensors.h. */
bool samplePressure(float *pressure_hpa, float *temperature_c)
{
  float pressure = 0.0f;
  int16_t temperature_centi_c = 0;
  int rc;

  *pressure_hpa = missing_sample();
  *temperature_c = missing_sample();

  rc = bmp581_config_forced_device(TAG_PRESSURE_DEVICE, UIUCTAG_PRESSURE_ODR,
                                   NULL);
  if (rc == 0) {
    rc = bmp581_sample_forced_blocking_device(TAG_PRESSURE_DEVICE,
                                              UIUCTAG_PRESSURE_TIMEOUT_US,
                                              &pressure,
                                              &temperature_centi_c);
  }

  /*
   * Power down on every path. bmp581_config_forced_device() leaves the rail on
   * so a caller may sleep until DRDY, so a failure between configuration and
   * readout must not leak that power into standby.
   */
  tagBusPowerOff(&TAG_PRESSURE_DEVICE->registers->bus);
  tagPressureDeviceAfterPowerOff(TAG_PRESSURE_DEVICE);

  if (rc != 0)
    return false;

  *pressure_hpa = pressure;
  *temperature_c = (float)temperature_centi_c / 100.0f;
  return true;
}
