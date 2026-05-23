/**
 * @file lps27_test.c
 * @brief Weak compatibility LPS27 self-test binding.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include <stdbool.h>
#include <stdint.h>

#include "lps27hhw.h"

/** @name LPS27 self-test binding
 * Compatibility default for tags that have not moved their test binding into
 * a tag/family device descriptor yet. Active LPS27 tags should override this
 * from devices.c so the test runs against the tag's TagPressureDevice.
 * @{
 */
/**
 * @brief Default disabled LPS27 self-test binding.
 *
 * @return false because no device descriptor is available here.
 */
bool __attribute__((weak)) tag_test_lps27(void)
{
  return false;
}
/** @} */
