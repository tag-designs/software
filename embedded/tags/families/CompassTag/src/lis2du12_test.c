/**
 * @file lis2du12_test.c
 * @brief CompassTag LIS2DU12 self-test hook.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "lis2du12.h"
#include "devices.h"

#include <stdbool.h>

/**
 * @brief Run the default LIS2DU12 presence test.
 *
 * @param[in] context Optional TagRegisterDevice descriptor.
 * @return true when the accelerometer identity register is valid.
 */
bool tag_test_lis2du12(const void *context)
{
  const TagRegisterDevice *device = context ? context : TAG_ACCEL_DEVICE;
  return lis2du12Test(device);
}
