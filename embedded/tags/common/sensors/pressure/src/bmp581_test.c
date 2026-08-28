/**
 * @file bmp581_test.c
 * @brief BMP581 self-test hook.
 * @author tag firmware authors
 * @date 2026-08-28
 */

#include "bmp581.h"
#include "devices.h"
#include "test_support.h"

/**
 * @brief Run the configured BMP581 presence test.
 *
 * @param[in] context Optional TagPressureDevice descriptor.
 * @return ALL_PASSED when the pressure sensor identity is valid, otherwise
 * LPS_FAILED.
 */
TestResult tag_test_bmp581(const void *context)
{
  const TagPressureDevice *device = context ? context : TAG_PRESSURE_DEVICE;
  return bmp581_check_who_am_i_device(device) ? ALL_PASSED : LPS_FAILED;
}
