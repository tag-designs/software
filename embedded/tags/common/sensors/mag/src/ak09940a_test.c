/**
 * @file ak09940a_test.c
 * @brief Weak production-test hook for the default AK09940A magnetometer.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include <stdbool.h>
#include <stdint.h>

#include "ak09940a.h"

/**
 * @brief Exercise the configured AK09940A as part of tag self-test.
 *
 * @param[in] context Optional TagRegisterDevice descriptor.
 * @return true when the default AK09940A descriptor reports a valid device.
 */
bool __attribute__((weak)) tag_test_ak09940a(const void *context)
{
  const TagRegisterDevice *device = context ? context : tagAk09940aDevice();
  return ak09940aTest(device);
}
