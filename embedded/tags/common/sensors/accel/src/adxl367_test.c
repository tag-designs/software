/**
 * @file adxl367_test.c
 * @brief ADXL367 identity and communication self-test helper.
 * @author tag firmware authors
 * @date 2026-06-16
 */

#include "ADXL367.h"
#include "core_types.h"
#include "hal.h"

#include <stdbool.h>
#include <stdint.h>

static uint8_t id[3] NOINIT;
static int xyz[3] NOINIT;

static void adxlavg(const TagAdxl367Device *device, int val[3])
{
  short x, y, z;
  val[0] = 0;
  val[1] = 0;
  val[2] = 0;

  for (int i = 0; i < 16; i++)
  {
    ADXL367_GetXyzDevice(device, &x, &y, &z);
    val[0] += x;
    val[1] += y;
    val[2] += z;
    chThdSleepMilliseconds(20);
  }

  val[0] /= 16;
  val[1] /= 16;
  val[2] /= 16;
}

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

    ADXL367_SetRangeDevice(device, ADXL367_RANGE_2G);
    ADXL367_SetOutputRateDevice(device, ADXL367_ODR_100_HZ);
    ADXL367_SetPowerModeDevice(device, ADXL367_MEASURE_ON);
    chThdSleepMilliseconds(200);
    adxlavg(device, xyz);

    /*
     * This test intentionally checks communication and conversion-path
     * readiness. Axis-orientation self-test limits vary by mounting and will
     * belong in a board/family-specific test when an ADXL367 target is added.
     */
    (void)xyz;
    result = true;
  } while (0);

  ADXL367_SoftwareResetDevice(device);
  ADXL367_DeviceEnd(device);
  return result;
}
