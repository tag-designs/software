/**
 * @file lsm6dsv16x_test.c
 * @brief Weak production-test hook for the LSM6DSV16X IMU accelerometer.
 * @author tag firmware authors
 * @date 2026-06-02
 */

#include "lsm6dsv16x.h"
#include "debug_log.h"
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
  lsm6dsv16x_self_test_result_t result;

  if (device == NULL)
  {
    debug_log_printf("LSM6DSV16X test: missing device context\r\n");
    return AIS2_FAILED;
  }

  result = lsm6dsv16x_self_test_accel(device);
  if (result == LSM6DSV16X_SELF_TEST_PASS)
  {
    return ALL_PASSED;
  }

  debug_log_printf("LSM6DSV16X test: reporting AIS2_FAILED for result %d\r\n",
                   (int)result);
  return AIS2_FAILED;
}
