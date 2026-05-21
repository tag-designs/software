#include "lps22hh.h"
#include "devices.h"

#include <stdbool.h>

bool tag_test_lps22hh(void)
{
  return lps22hh_check_who_am_i_device(TAG_PRESSURE_DEVICE);
}
