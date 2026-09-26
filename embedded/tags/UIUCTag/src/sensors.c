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

/*
 * ADXL367 wake-mode configuration below is a direct, verbatim port of
 * BitTagNG's initActivitySensor() (embedded/tags/families/BitTagNG/src/
 * sensors.c) -- same register values, same constants, same bypass of the
 * shared ADXL367_Setup*Device() helpers in favor of raw register writes.
 * BitTagNG's ADXL367 wake configuration is field-validated; the only
 * intentional hardware difference between the two boards is which
 * interrupt pin carries AWAKE to the MCU wakeup line, and both boards
 * already agree on that (INTMAP1/INT1). Do not "correct" any of these
 * values against the data sheet independently of BitTagNG -- a data-sheet
 * rereading of the threshold register format, and two different attempts
 * at deriving an equivalent inactivity threshold from first principles,
 * each independently failed to reproduce BitTagNG's working behavior.
 */
#define UIUCTAG_ADXL_ACT_COUNT 2
#define UIUCTAG_ADXL_DEFAULT_MODE(active, inactive) (((inactive) << 2) | (active))
#define UIUCTAG_ADXL_REFERENCED_ABSOLUTE_LOOP_MODE                           \
  (ADXL367_ACT_INACT_CTL_LINKLOOP(ADXL367_MODE_LOOP) |                      \
   UIUCTAG_ADXL_DEFAULT_MODE(ADXL367_REFERENCED_ACTIVITY_ENABLE,            \
                             ADXL367_INACTIVITY_ENABLE))
#define UIUCTAG_ADXL_SAMPLE_RATE ADXL367_ODR_12P5HZ
#define UIUCTAG_ADXL_WAKEUP_RATE_6P25HZ 1u
#define UIUCTAG_ADXL_TIMER_CTL_WAKEUP_RATE_SHIFT 6u
#define UIUCTAG_ADXL_TIMER_CTL_WAKEUP_RATE(rate) \
  (((rate) & 0x3u) << UIUCTAG_ADXL_TIMER_CTL_WAKEUP_RATE_SHIFT)
#define UIUCTAG_ADXL_WAKEUP_TIMER_CTL \
  UIUCTAG_ADXL_TIMER_CTL_WAKEUP_RATE(UIUCTAG_ADXL_WAKEUP_RATE_6P25HZ)
/** BitTagNG's own register-position shift for THRESH_ACT/THRESH_INACT. */
#define UIUCTAG_ADXL_THRESH_RANGE_SHIFT (2 + 2)
/** BitTagNG's hardcoded inactivity threshold; not host-configurable there. */
#define UIUCTAG_ADXL_INACT_THRESH_MG 1100U
#define UIUCTAG_ADXL_UINT16SWAP(x) \
  ((((x) & 0xffU) << 8) | (((x) >> 8) & 0xffU))

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
  unsigned int act_thresh = sconfig.adxl_act_thresh_cnt << UIUCTAG_ADXL_THRESH_RANGE_SHIFT;

  ADXL367_DeinitDevice(TAG_ACCEL_DEVICE);

  ADXL367_DeviceBegin(TAG_ACCEL_DEVICE);

  /*
   * ADXL367_DeinitDevice() only disables/clears POWER_CTL, the interrupt
   * maps, and ACT_INACT_CTL -- it does not reset the chip's internal
   * loop-mode activity/inactivity state machine, which is not powered from
   * an MCU-controlled rail and so survives every MCU reset, reflash, and
   * exception-recovery re-init that doesn't also power-cycle the sensor.
   * Observed on the bench: the AWAKE line stayed asserted from boot across
   * several MCU resets, matching a chip that was left latched in its
   * "active" loop-state by earlier handling, and only cleared after a real
   * shake carried it through a genuine activity/inactivity cycle. A real
   * soft reset (already used by the self-test, adxl367_test.c) guarantees a
   * known state independent of that history. 100 ms mirrors the self-test's
   * own post-reset settling delay before the chip is touched again.
   */
  ADXL367_SoftwareResetDevice(TAG_ACCEL_DEVICE);
  chThdSleepMilliseconds(100);

  ADXL367_SetPowerModeDevice(TAG_ACCEL_DEVICE, ADXL367_MEASURE_STANDBY);

  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, UIUCTAG_ADXL_SAMPLE_RATE,
                                 ADXL367_REG_FILTER_CTL, 1);

  // Set the configured activity threshold.
  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE,
                                 UIUCTAG_ADXL_UINT16SWAP(act_thresh),
                                 ADXL367_REG_THRESH_ACT_H, 2);

  // Set inactivity threshold to 1100 mg.
  ADXL367_SetRegisterValueDevice(
      TAG_ACCEL_DEVICE,
      UIUCTAG_ADXL_UINT16SWAP(UIUCTAG_ADXL_INACT_THRESH_MG
                             << UIUCTAG_ADXL_THRESH_RANGE_SHIFT),
      ADXL367_REG_THRESH_INACT_H, 2);

  // Enable looped referenced activity and absolute inactivity detection.
  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE,
                                 UIUCTAG_ADXL_REFERENCED_ABSOLUTE_LOOP_MODE,
                                 ADXL367_REG_ACT_INACT_CTL, 1);

  // Timer control register: wakeup mode at 6.25 samples/second.
  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, UIUCTAG_ADXL_WAKEUP_TIMER_CTL,
                                 ADXL367_REG_TIMER_CTL, 1);

  // Set inactivity timer.
  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE,
                                 UIUCTAG_ADXL_UINT16SWAP(sconfig.adxl_inactive_samples),
                                 ADXL367_REG_TIME_INACT_H, 2);

  // Set the activity timer.
  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, UIUCTAG_ADXL_ACT_COUNT,
                                 ADXL367_REG_TIME_ACT, 1);

  // Route the ADXL367 awake state as a level signal for MCU wakeup (INT1,
  // the wakeup line on both this board and BitTagNG's).
  ADXL367_SetRegisterValueDevice(TAG_ACCEL_DEVICE, ADXL367_INTMAP1_AWAKE,
                                 ADXL367_REG_INTMAP1_LWR, 1);

  // Start measurement mode with wakeup enabled.
  ADXL367_SetRegisterValueDevice(
      TAG_ACCEL_DEVICE,
      ADXL367_POWER_CTL_WAKEUP | ADXL367_POWER_CTL_MEASURE(ADXL367_MEASURE_ON),
      ADXL367_REG_POWER_CTL, 1);

  /*
   * BitTagNG's initActivitySensor() reads STATUS here, right after the
   * standby-to-measure transition, before ending the device session -- this
   * step was missing from this port. Without it the AWAKE pin was observed
   * stuck asserted (isActive read true) from boot until a real shake, which
   * is consistent with the transition leaving STATUS/the pin state
   * unsettled until something (a status read, or a real activity event)
   * forces it to latch correctly.
   */
  {
    uint8_t status;
    ADXL367_GetRegisterValueDevice(TAG_ACCEL_DEVICE, &status,
                                   ADXL367_REG_STATUS, 1);
  }

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
