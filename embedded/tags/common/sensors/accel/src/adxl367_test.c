/**
 * @file adxl367_test.c
 * @brief ADXL367 identity, communication, and electrostatic self-test helper.
 * @author tag firmware authors
 * @date 2026-06-16
 */

#include "ADXL367.h"
#include "core_types.h"
#include "debug_log.h"
#include "hal.h"
#include "tag.pb.h"

#include <stdbool.h>
#include <stdint.h>

static uint8_t id[3] NOINIT;

/**
 * @brief 0.25 mg/LSB sensitivity at the +/-2g range (data sheet Table 1,
 *        "Sensitivity", 2g range), the range this self test forces the
 *        device into.
 */
#define ADXL367_SELF_TEST_MG_PER_LSB 0.25f

/**
 * @brief Settle time after entering measurement mode, before the first
 *        register readback.
 *
 * @details The data sheet's "Measurement Mode Instruction to First Sample" is
 *          100 ms typical (Table 1, Turn-On Time). Measured on real UIUCTag
 *          hardware, STATUS.DATA_READY did not assert until well past that:
 *          100 ms produced STATUS=0x40 (no DATA_READY) and all-zero XDATA;
 *          600 ms reliably produced STATUS=0x41 and a real sample.
 */
#define ADXL367_MEASURE_SETTLE_MS 600U

/**
 * @brief Settle time between self-test steps.
 *
 * @details The data sheet specifies 4/ODR (Steps 3 and 6 of "Using Self
 *          Test"), 40 ms at the 100 Hz ODR this test forces. Measured on real
 *          UIUCTag hardware that was too short to see any self-test force
 *          response (x_before == x_forced); 300 ms reliably produced a
 *          response matching the data sheet's typical 180 mg.
 */
#define ADXL367_SELF_TEST_SETTLE_MS 300U

/**
 * @brief Minimum acceptable |x_forced - x_before| self-test response, in mg.
 *
 * @details The data sheet's Table 1 self-test output change spec (90 mg to
 *          270 mg typical 180 mg) is only guaranteed at Vs = 2.0 V; the
 *          "Operation at Voltages Other Than 2.0 V" section notes the
 *          response is not proportional to supply voltage and gives an
 *          approximate 170 mg figure at other voltages. Tags run the
 *          ADXL367 off the board supply, not a regulated 2.0 V rail, so this
 *          threshold is set well below the data sheet minimum -- enough to
 *          confirm the MEMS structure and self-test force circuit respond at
 *          all, not to reproduce the exact spec window.
 */
#define ADXL367_SELF_TEST_MIN_DELTA_MG 40.0f

/**
 * @brief Read the x-axis acceleration sample, discarding y and z.
 *
 * @param[in] device Accelerometer device descriptor.
 * @return Signed 14-bit x-axis code, in the units of the currently
 *         configured measurement range.
 */
static int16_t adxl367ReadXDevice(const TagAdxl367Device *device)
{
  short x = 0;
  short y = 0;
  short z = 0;

  ADXL367_GetXyzDevice(device, &x, &y, &z);
  (void)y;
  (void)z;
  return x;
}

/**
 * @brief Run the ADXL367's electrostatic self test and check its x-axis
 *        response.
 *
 * @details Implements the data sheet's "Using Self Test" procedure exactly:
 *          enter measurement mode, settle, enable ST, settle, sample x,
 *          enable ST_FORCE alongside ST, settle, sample x again, then
 *          disable self test. The difference between the two x samples,
 *          converted to mg, is the self-test force response.
 *
 *          Only the x-axis is checked because Table 1 only specifies a
 *          self-test output change for X_OUT; y and z have no guaranteed
 *          self-test response. Only the +/-2g range is used because the
 *          data sheet states self test is most accurate there and may be
 *          inaccurate at +/-4g/+/-8g due to low signal levels.
 *
 * @param[in] device Accelerometer device descriptor, already begun.
 * @return true when the x-axis self-test response meets
 *         ADXL367_SELF_TEST_MIN_DELTA_MG.
 */
static bool adxl367SelfTestForceDevice(const TagAdxl367Device *device)
{
  int16_t x_before;
  int16_t x_forced;
  float delta_mg;

  ADXL367_SetRangeDevice(device, ADXL367_RANGE_2G);
  ADXL367_SetOutputRateDevice(device, ADXL367_ODR_100_HZ);
  ADXL367_SetPowerModeDevice(device, ADXL367_MEASURE_ON);
  chThdSleepMilliseconds(ADXL367_MEASURE_SETTLE_MS); /* Step 1. */

  {
    uint8_t power_ctl = 0xFFU;
    uint8_t filter_ctl = 0xFFU;
    uint8_t status = 0xFFU;

    ADXL367_GetRegisterValueDevice(device, &power_ctl, ADXL367_REG_POWER_CTL, 1);
    ADXL367_GetRegisterValueDevice(device, &filter_ctl, ADXL367_REG_FILTER_CTL, 1);
    ADXL367_GetRegisterValueDevice(device, &status, ADXL367_REG_STATUS, 1);
    debug_log_printf("ADXL367: power_ctl=0x%02x filter_ctl=0x%02x status=0x%02x\r\n",
                     power_ctl, filter_ctl, status);
  }

  ADXL367_SetRegisterValueDevice(device, ADXL367_SELF_TEST_ST,
                                 ADXL367_REG_SELF_TEST, 1); /* Step 2. */
  chThdSleepMilliseconds(ADXL367_SELF_TEST_SETTLE_MS);      /* Step 3. */
  x_before = adxl367ReadXDevice(device);                    /* Step 4. */

  ADXL367_SetRegisterValueDevice(
      device, ADXL367_SELF_TEST_ST | ADXL367_SELF_TEST_ST_FORCE,
      ADXL367_REG_SELF_TEST, 1);                       /* Step 5. */
  chThdSleepMilliseconds(ADXL367_SELF_TEST_SETTLE_MS); /* Step 6. */
  x_forced = adxl367ReadXDevice(device);                /* Step 7. */

  ADXL367_SetRegisterValueDevice(device, 0, ADXL367_REG_SELF_TEST,
                                 1); /* Step 9: disable ST and ST_FORCE. */

  delta_mg = (float)(x_forced - x_before) * ADXL367_SELF_TEST_MG_PER_LSB;
  if (delta_mg < 0.0f)
    delta_mg = -delta_mg;

  debug_log_printf("ADXL367: self test x_before=%d x_forced=%d delta=%d mg"
                   " (minimum %d mg)\r\n",
                   x_before, x_forced, (int)delta_mg,
                   (int)ADXL367_SELF_TEST_MIN_DELTA_MG);

  return delta_mg >= ADXL367_SELF_TEST_MIN_DELTA_MG;
}

/**
 * @brief Check ADXL367 identity and run its electrostatic self test.
 *
 * @param[in] device Accelerometer device descriptor.
 * @return true when the device identifies correctly and its self-test x-axis
 *         response meets ADXL367_SELF_TEST_MIN_DELTA_MG.
 */
bool adxl367Test(const TagAdxl367Device *device)
{
  bool result = false;
  ADXL367_DeviceBegin(device);

  ADXL367_SoftwareResetDevice(device);
  do
  {
    chThdSleepMilliseconds(100);

    ADXL367_GetRegisterValueDevice(device, id, ADXL367_REG_DEVID_AD, 3);
    if ((id[0] != ADXL367_DEVICE_AD) ||
        (id[1] != ADXL367_DEVICE_MST) ||
        (id[2] != ADXL367_PART_ID))
    {
      break;
    }

    result = adxl367SelfTestForceDevice(device);
  } while (0);

  ADXL367_SoftwareResetDevice(device);
  ADXL367_DeviceEnd(device);
  return result;
}

TestResult tag_test_adxl367(const void *context)
{
  return adxl367Test((const TagAdxl367Device *)context)
             ? ALL_PASSED
             : ADXL362_FAILED;
}
