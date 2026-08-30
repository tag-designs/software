/**
 * @file bmp581_test.c
 * @brief BMP581 self-test hook.
 * @author tag firmware authors
 * @date 2026-08-28
 */

#include "bmp581.h"
#include "test_support.h"

/**
 * @brief Run the configured BMP581 presence test.
 *
 * @param[in] context TagPressureDevice descriptor.
 * @return ALL_PASSED when the pressure sensor identity is valid, otherwise
 * LPS_FAILED.
 */
TestResult tag_test_bmp581(const void *context)
{
  if (!context)
    return LPS_FAILED;

  const TagPressureDevice *device = (const TagPressureDevice *)context;
  return bmp581_check_who_am_i_device(device) ? ALL_PASSED : LPS_FAILED;
}
