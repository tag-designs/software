/**
 * @file custom.h
 * @brief CompassTag MX25R variant build constants.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef CUSTOM_H
#define CUSTOM_H

/** @name Firmware identity
 * Strings and protocol sizing reported to host tools.
 * @{
 */
#define FIRMWARE_STRING "CompassTagv1, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "CompassTagv1"
#define QTMONITOR_VERSION 2.0
#define PROTOBUFSIZE 4096
/** @} */

/** @name Sensor conversion constants
 * Reported to host tools in TagInfo (accelconstant, magconstant) and in the
 * tag identity record. Nothing in the firmware or the host converts data with
 * them: datalog.c, sensors.c and the host capture decoder use literals.
 *
 * Both describe the live calibration stream from sensorCalibrationSample(),
 * which is not the same as the data log for the magnetometer:
 * - ACCEL_CONSTANT is mg per count of the 12-bit LIS2DU12 reading at +-2 g.
 *   The data log stores the same 12-bit counts, so it applies there too.
 * - MAG_CONSTANT is uT per count of the full 18-bit AK09940A reading
 *   (AK09940A_SENSITIVITY_UT_PER_LSB). The data log keeps only the top 16
 *   bits, so one logged count is 4 * MAG_CONSTANT = 0.04 uT. Converting
 *   logged counts with MAG_CONSTANT gives fields four times too small.
 * @{
 */
/** @brief mg per 12-bit accelerometer count, live stream and data log. */
#define ACCEL_CONSTANT 0.976f
/** @brief uT per 18-bit calibration-stream count; a logged count is 0.04 uT. */
#define MAG_CONSTANT 0.01
/** @} */

#endif
