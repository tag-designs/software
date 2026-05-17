#include "ak09940.h"

#include <stdbool.h>

bool tag_test_ak09940a(void)
{
  return ak09940_self_test();
}
