/**
 * @file test_support.h
 * @brief Shared self-test runner interfaces for tag and family devices.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_TEST_SUPPORT_H
#define TAG_CORE_TEST_SUPPORT_H

#include <stdbool.h>
#include <stddef.h>

#include "tag.pb.h"
#include "tagdata.pb.h"

/** @name Self-test descriptors
 * Tags provide a table of these descriptors so the common self-test runner can
 * execute hardware-specific checks without owning device descriptors.
 * @{
 */
typedef struct
{
  TestReq request;
  TestResult (*run)(const void *context);
  const void *context;
} TagTestCase;
/** @} */

/** @name Self-test runner
 * Entry points used by the state machine and monitor to run and report tests.
 * @{
 */
/**
 * @brief Run requested tag self-tests and store the first failure in backup state.
 */
void test(void);

/**
 * @brief Return the most recent self-test result.
 *
 * @return Stored self-test result from backup state.
 */
TestResult testreport(void);

extern TestReq test_to_run;

/**
 * @brief Return the tag/family-provided self-test table.
 *
 * @param[out] count Number of entries in the returned table.
 * @return Pointer to the immutable test-case table.
 */
const TagTestCase *tagTestCases(size_t *count);
/** @} */

/** @name Optional device self-tests
 * Weak or tag-provided tests referenced by family test tables.
 * @{
 */
/**
 * @brief Test ADXL362 accelerometer communication and readiness.
 *
 * @return ALL_PASSED when the device passes, otherwise a device-specific
 * failure result.
 */
TestResult tag_test_adxl362(const void *context);

/**
 * @brief Test LIS2DU12 accelerometer communication and readiness.
 *
 * @return ALL_PASSED when the device passes, otherwise a device-specific
 * failure result.
 */
TestResult tag_test_lis2du12(const void *context);

/**
 * @brief Test LSM6DSV16X IMU accelerometer communication and self-test.
 *
 * @return ALL_PASSED when the accelerometer self-test passes, otherwise the
 * accel-family failure result.
 */
TestResult tag_test_lsm6dsv16x(const void *context);

/**
 * @brief Test AK09940A magnetometer communication and readiness.
 *
 * @return ALL_PASSED when the device passes, otherwise a device-specific
 * failure result.
 */
TestResult tag_test_ak09940a(const void *context);

/**
 * @brief Test RTC communication and timekeeping readiness.
 *
 * @return ALL_PASSED when the RTC passes, otherwise RTC_FAILED.
 */
TestResult tag_test_rtc(const void *context);

/**
 * @brief Test external flash communication and storage readiness.
 *
 * @return ALL_PASSED when the flash passes, otherwise the selected flash
 * device's failure result.
 */
TestResult tag_test_external_flash(const void *context);

/**
 * @brief Test LPS27 pressure sensor communication and readiness.
 *
 * @return ALL_PASSED when the pressure sensor passes, otherwise LPS27_FAILED.
 */
TestResult tag_test_lps27(const void *context);

/**
 * @brief Test LPS22HH pressure sensor communication and readiness.
 *
 * @return ALL_PASSED when the pressure sensor passes, otherwise LPS22_FAILED.
 */
TestResult tag_test_lps22hh(const void *context);
/** @} */

#endif
