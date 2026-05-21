#include <stdbool.h>
#include <stdint.h>

#include "lps27hhw.h"

/*
 * Compatibility default for tags that have not moved their test binding into
 * a tag/family device descriptor yet. Active LPS27 tags should override this
 * from devices.c so the test runs against the tag's TagPressureDevice.
 */
bool __attribute__((weak)) tag_test_lps27(void)
{
  return false;
}
