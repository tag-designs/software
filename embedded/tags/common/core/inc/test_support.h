#ifndef TAG_CORE_TEST_SUPPORT_H
#define TAG_CORE_TEST_SUPPORT_H

#include <stdbool.h>

#include "tag.pb.h"
#include "tagdata.pb.h"

void test(void);
TestResult testreport(void);

extern TestReq test_to_run;

#if defined(TAG_SENSOR_ACCEL_ADXL362)
bool tag_test_adxl362(void);
#endif

#if defined(TAG_SENSOR_ACCEL_LIS2DU12)
bool tag_test_lis2du12(void);
#endif

#if defined(TAG_SENSOR_MAG_AK09940A)
bool tag_test_ak09940a(void);
#endif

#if defined(TAG_RTC_RV3028)
bool tag_test_rtc(void);
#endif

#if defined(TAG_HAS_EXTERNAL_FLASH)
bool tag_test_external_flash(void);
#endif

#if defined(TAG_SENSOR_PRESSURE_LPS27)
bool tag_test_lps27(void);
#endif

#if defined(TAG_SENSOR_PRESSURE_LPS22HH)
bool tag_test_lps22hh(void);
#endif

#endif
