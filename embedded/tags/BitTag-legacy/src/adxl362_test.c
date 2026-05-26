#include "ADXL362.h"
#include "core_types.h"
#include "hal.h"
#include "power.h"
#include "test_support.h"

#include <stdbool.h>
#include <stdint.h>

static uint8_t id[3] NOINIT;
static int xyz[3] NOINIT;
static int testxyz[3] NOINIT;

static void adxlavg(int val[3])
{
  short x, y, z;
  val[0] = 0;
  val[1] = 0;
  val[2] = 0;

  for (int i = 0; i < 16; i++)
  {
    ADXL362_GetXyz(&x, &y, &z);
    val[0] += x;
    val[1] += y;
    val[2] += z;
    chThdSleepMilliseconds(20);
  }

  val[0] /= 16;
  val[1] /= 16;
  val[2] /= 16;
}

TestResult tag_test_adxl362(const void *context)
{
  (void)context;
  bool result = false;
  accelSpiOn();

  ADXL362_SoftwareReset();
  do
  {
    chThdSleepMilliseconds(100);

    ADXL362_GetRegisterValue(id, ADXL362_REG_DEVID_AD, 3);
    if ((id[0] != 0xAD) || (id[1] != 0x1D) || (id[2] != 0xF2))
    {
      break;
    }

    ADXL362_SetRegisterValue(0x83, 0x2C, 1);
    ADXL362_SetRegisterValue(0x02, 0x2D, 1);
    chThdSleepMilliseconds(200);
    adxlavg(xyz);

    ADXL362_SetRegisterValue(0x00, 0x2D, 1);
    ADXL362_SetRegisterValue(0x01, 0x2E, 1);
    ADXL362_SetRegisterValue(0x02, 0x2D, 1);
    chThdSleepMilliseconds(200);
    adxlavg(testxyz);
    ADXL362_SetRegisterValue(0x00, 0x2D, 1);

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

  ADXL362_SoftwareReset();
  accelSpiOff();
  return result ? ALL_PASSED : ADXL362_FAILED;
}
