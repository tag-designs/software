#include <stdbool.h>
#include <stdint.h>

#include "ak09940a.h"

bool tag_test_ak09940a(void)
{
  return ak09940aTest(tagAk09940aDevice());
}
