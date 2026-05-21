#ifndef TAG_CORE_TEST_SUPPORT_H
#define TAG_CORE_TEST_SUPPORT_H

#include <stdbool.h>
#include <stddef.h>

#include "tag.pb.h"
#include "tagdata.pb.h"

typedef struct
{
  TestReq request;
  TestResult failure;
  bool (*run)(void);
} TagTestCase;

void test(void);
TestResult testreport(void);

extern TestReq test_to_run;

const TagTestCase *tagTestCases(size_t *count);

bool tag_test_adxl362(void);
bool tag_test_lis2du12(void);
bool tag_test_ak09940a(void);
bool tag_test_rtc(void);
bool tag_test_external_flash(void);
bool tag_test_lps27(void);
bool tag_test_lps22hh(void);

#endif
