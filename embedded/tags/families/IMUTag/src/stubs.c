/**
 * @file stubs.c
 * @brief Placeholder accelerometer hooks for inactive IMUTagBreakout paths.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include <stdint.h>
#include <stdbool.h>

/** @brief Placeholder deinit hook for builds without an active accelerometer. */
void accelDeinit(void) {}
/** @brief Placeholder init hook for builds without an active accelerometer. */
void accelInit(void){}
/**
 * @brief Placeholder self-test hook for builds without an active accelerometer.
 *
 * @return false because no accelerometer is configured through this path.
 */
bool accelTest(void){return false;}
