/**
 * @file lps22hh_test.c
 * @brief IMUTagBreakout LPS22HH self-test hook.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "lps22hh.h"
#include "devices.h"
#include "test_support.h"

#include <stdbool.h>

/**
 * @brief Run the configured LPS22HH presence test.
 *
 * @param[in] context Optional TagPressureDevice descriptor.
 * @return ALL_PASSED when the pressure sensor identity is valid, otherwise
 * LPS22HH_FAILED.
 */
TestResult tag_test_lps22hh(const void *context)
{
  const TagPressureDevice *device = context ? context : TAG_PRESSURE_DEVICE;
  return lps22hh_check_who_am_i_device(device) ? ALL_PASSED : LPS22HH_FAILED;
}
