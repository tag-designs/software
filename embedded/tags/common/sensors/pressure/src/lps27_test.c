#include <stdbool.h>
#include <stdint.h>

#include "lps.h"

/*
 * Compatibility default for tags that have not moved their test binding into
 * a tag/family device descriptor yet.
 */
bool __attribute__((weak)) tag_test_lps27(void)
{
  return lpsTest();
}
