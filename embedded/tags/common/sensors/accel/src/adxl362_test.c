/**
 * @file adxl362_test.c
 * @brief ADXL362 identity and self-test helper.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "ADXL362.h"
#include "core_types.h"
#include "hal.h"
#include "power.h"

#include <stdbool.h>
#include <stdint.h>

static uint8_t id[3] NOINIT;
static int xyz[3] NOINIT;
static int testxyz[3] NOINIT;

/** @name ADXL362 self-test helpers
 * Helpers used by tag test tables to verify communication and physical
 * self-test response.
 * @{
 */
/**
 * @brief Average a small batch of acceleration samples.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[out] val Three-element averaged X/Y/Z output.
 */
static void adxlavg(const TagAdxl362Device *device, int val[3])
{
  short x, y, z;
  val[0] = 0;
  val[1] = 0;
  val[2] = 0;

  for (int i = 0; i < 16; i++)
  {
    ADXL362_GetXyzDevice(device, &x, &y, &z);
    val[0] += x;
    val[1] += y;
    val[2] += z;
    chThdSleepMilliseconds(20);
  }

  val[0] /= 16;
  val[1] /= 16;
  val[2] /= 16;
}

/**
 * @brief Run the ADXL362 identity and self-test sequence.
 *
 * @param[in] device ADXL362 device descriptor.
 * @return true when identity and self-test pass.
 */
bool adxl362Test(const TagAdxl362Device *device)
{
  bool result = false;
  ADXL362_DeviceBegin(device);

  ADXL362_SoftwareResetDevice(device);
  do
  {
    chThdSleepMilliseconds(100);

    ADXL362_GetRegisterValueDevice(device, id, ADXL362_REG_DEVID_AD, 3);
    if ((id[0] != 0xAD) || (id[1] != 0x1D) || (id[2] != 0xF2))
    {
      break;
    }

    ADXL362_SetRegisterValueDevice(device, 0x83, 0x2C, 1);
    ADXL362_SetRegisterValueDevice(device, 0x02, 0x2D, 1);
    chThdSleepMilliseconds(200);
    adxlavg(device, xyz);

    ADXL362_SetRegisterValueDevice(device, 0x00, 0x2D, 1);
    ADXL362_SetRegisterValueDevice(device, 0x01, 0x2E, 1);
    ADXL362_SetRegisterValueDevice(device, 0x02, 0x2D, 1);
    chThdSleepMilliseconds(200);
    adxlavg(device, testxyz);
    ADXL362_SetRegisterValueDevice(device, 0x00, 0x2D, 1);

    int tmp = testxyz[0] - xyz[0];
    if ((tmp < 50) || (tmp > 700))
    {
      break;
    }
    tmp = testxyz[1] - xyz[1];
    if ((tmp < -700) || (tmp > -50))
    {
      break;
    }
    tmp = testxyz[2] - xyz[2];
    if ((tmp < 50) || (tmp > 700))
    {
      break;
    }

    result = true;
  } while (0);

  ADXL362_SoftwareResetDevice(device);
  ADXL362_DeviceEnd(device);
  return result;
}

/**
 * @brief Default ADXL362 self-test binding for the configured tag device.
 *
 * @return true when the configured ADXL362 passes self-test.
 */
bool __attribute__((weak)) tag_test_adxl362(void)
{
  return adxl362Test(tagAdxl362Device());
}
/** @} */
