/**
 * @file ak09940_test.c
 * @brief Legacy IMUTagBreakout AK09940 self-test hook.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "ak09940.h"

#include <stdbool.h>

/**
 * @brief Run the legacy AK09940 self-test.
 *
 * @return true when the magnetometer self-test passes.
 */
bool tag_test_ak09940a(void)
{
  return ak09940_self_test();
}
