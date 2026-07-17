/**
 * @file bmm350_test.c
 * @brief Weak production-test hook for the BMM350 magnetometer.
 * @author tag firmware authors
 * @date 2026-07-17
 */

#include "bmm350_tag.h"
#include "test_support.h"

/**
 * @brief Exercise the configured BMM350 as part of tag self-test.
 *
 * @param[in] context TagBmm350Device descriptor.
 * @return ALL_PASSED when the BMM350 descriptor reports a valid device,
 * otherwise AK09940A_FAILED until a BMM350-specific protobuf result exists.
 */
TestResult __attribute__((weak)) tag_test_bmm350(const void *context)
{
  const TagBmm350Device *device = (const TagBmm350Device *)context;
  bool ok = false;

  if (device != NULL && device->compensation != NULL) {
    bmm350DeviceBegin(device);
    ok = bmm350CheckWhoami(device);
    if (ok)
      ok = bmm350Reset(device) == MSG_OK && bmm350CheckWhoami(device);
    if (ok)
      ok = bmm350ReadCompensationData(device) == MSG_OK;
    if (ok)
      (void)bmm350InitPowerDown(device);
    bmm350DeviceEnd(device);
  }

  return ok ? ALL_PASSED : AK09940A_FAILED;
}
