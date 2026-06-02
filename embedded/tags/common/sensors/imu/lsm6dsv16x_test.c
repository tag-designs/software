/**
 * @file lsm6dsv16x_test.c
 * @brief Weak production-test hook for the LSM6DSV16X IMU accelerometer.
 * @author tag firmware authors
 * @date 2026-06-02
 */

#include "lsm6dsv16x.h"
#include "test_support.h"

/**
 * @brief Exercise the configured LSM6DSV16X accelerometer self-test.
 *
 * @param[in] context Optional TagLsm6dsv16xDevice descriptor.
 * @return ALL_PASSED when the accelerometer self-test passes, otherwise
 * AIS2_FAILED. The monitor protocol still uses the accel-family result here.
 */
TestResult __attribute__((weak)) tag_test_lsm6dsv16x(const void *context)
{
  const TagLsm6dsv16xDevice *device = context;

  if (device == NULL)
  {
    return AIS2_FAILED;
  }

  return lsm6dsv16x_self_test_accel(device) == LSM6DSV16X_SELF_TEST_PASS
             ? ALL_PASSED
             : AIS2_FAILED;
}
