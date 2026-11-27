/**
 * @file accel_adxl367.c
 * @brief UIUCadxl367 accelerometer: ADXL367 wake-mode activity detection.
 * @author tag firmware authors
 * @date 2026-08-31
 *
 * @details initDataCollection() for boards fitted with an ADXL367. Selected by
 *          the UIUCadxl367 target's project.mk; UIUCTag itself carries an
 *          ADXL362 and compiles accel_adxl362.c instead. Both implement the
 *          same contract (sensors.h) against the same stored configuration:
 *          activity threshold in mg, inactivity as a sample count at the
 *          ~6 Hz wake rate, and a fixed 1100 mg absolute inactivity
 *          threshold.
 *
 * @note    The ADXL367 sits on USART2 in synchronous 4-wire SPI mode; the
 *          binding lives in devices.c, so this file is transport-agnostic.
 *
 * @see     ../design/data-collection.md
 */

#include "hal.h"
#include <stdbool.h>

#include "tag.pb.h"
#include "config.h"
#include "devices.h"
#include "sensors.h"

#include "ADXL367.h"

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
