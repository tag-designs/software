/**
 * @file lps22hh_test.c
 * @brief IMUTagBreakout LPS22HH self-test hook.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "lps22hh.h"
#include "devices.h"

#include <stdbool.h>

/**
 * @brief Run the configured LPS22HH presence test.
 *
 * @return true when the pressure sensor identity is valid.
 */
bool tag_test_lps22hh(void)
{
  return lps22hh_check_who_am_i_device(TAG_PRESSURE_DEVICE);
}
