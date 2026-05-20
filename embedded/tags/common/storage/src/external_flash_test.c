#include <stdbool.h>
#include <stdint.h>

#include "external_flash.h"

/*
 * Compatibility default for tags that have not moved their test binding into
 * a tag/family storage descriptor yet.
 */
bool __attribute__((weak)) tag_test_external_flash(void)
{
  ExFlashPwrUp();
  bool result = ExCheckID() > -1;
  ExFlashPwrDown();
  return result;
}
