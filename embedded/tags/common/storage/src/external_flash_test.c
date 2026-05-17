#include <stdbool.h>
#include <stdint.h>

#include "external_flash.h"

bool tag_test_external_flash(void)
{
  ExFlashPwrUp();
  bool result = ExCheckID() > -1;
  ExFlashPwrDown();
  return result;
}
