/**
 * @file lps27_test.c
 * @brief Weak compatibility LPS27 self-test binding.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include <stdbool.h>
#include <stdint.h>

#include "lps27hhw.h"
#include "test_support.h"

/** @name LPS27 self-test binding
 * Compatibility default for tags that have not moved their test binding into
 * a tag/family device descriptor yet. Active LPS27 tags should override this
 * from devices.c so the test runs against the tag's TagPressureDevice.
 * @{
 */
/**
 * @brief LPS27 self-test binding for a test-case pressure descriptor.
 *
 * @param[in] context TagPressureDevice descriptor.
 * @return ALL_PASSED when the LPS27 device responds and samples correctly,
 * otherwise LPS27_FAILED.
 */
TestResult __attribute__((weak)) tag_test_lps27(const void *context)
{
  if (context == NULL)
  {
    return LPS27_FAILED;
  }

  return lps27Test((const TagPressureDevice *)context) ? ALL_PASSED : LPS27_FAILED;
}
/** @} */
