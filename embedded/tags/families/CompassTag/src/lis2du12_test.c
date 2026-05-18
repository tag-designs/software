#include "lis2du12.h"

#include <stdbool.h>

bool tag_test_lis2du12(void)
{
  return lis2du12Test(tagLis2du12Device());
}
