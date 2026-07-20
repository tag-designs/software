/**
 * @file custom.h
 * @brief IMUTagNandv1 variant build constants and board line aliases.
 * @author tag firmware authors
 * @date 2026-07-20
 */

#ifndef CUSTOM_H
#define CUSTOM_H

/** @name Storage and firmware identity
 * External flash sizing, strings, protocol sizing, and feature flags.
 * @{
 */
#define EXT_FLASH_SIZE (1024UL * 1024UL * 128UL)

#define FIRMWARE_STRING "IMUTagNandv1, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "IMUTagNandv1"
#define USE_LPS22HH 1
#define SENSOR_CALIBRATION 1
#define QTMONITOR_VERSION 2.0
#define PROTOBUFSIZE 4096
#define SENSOR_CONSTANTS 1
#define CALIBRATION_CONSTANTS 1
#define USE_STOP1 1
#define STOP1_WAKE_EXTI_GROUP1_MASK (1U << 0)
#define CONFIG_HAS_HIBERNATE 0
#define TAG_STATUS_FIXED_VDD100 300
#define TAG_IMUTAG_RTC_I2C_HARDWARE 1
#define TAG_IMUTAG_I2C_TIMINGR 0x00000509U
/** @} */

/** @name Shared I2C bus aliases
 * The RTC and BMM350 share the hardware I2C1 bus on the generated SDA/SCL lines.
 * @{
 */
#define LINE_RTC_SDA LINE_SDA
#define LINE_RTC_SCL LINE_SCL
#define BMM350_I2C_ADDRESS 0x14U
/** @} */

/** @name IMUTag family device line bindings
 * The IMUTag family source predates this board's signal names, so the variant
 * selects its concrete flash, IMU, pressure, and BMM350 lines here.
 * @{
 */
#define IMUTAG_FLASH_CS_LINE LINE_FLASH_nCS
#define IMUTAG_FLASH_SCK_LINE LINE_LSM_FLASH_SCK
#define IMUTAG_FLASH_MISO_LINE LINE_LSM_FLASH_MISO
#define IMUTAG_FLASH_MOSI_LINE LINE_LSM_FLASH_MOSI

#define IMUTAG_LPS_CS_LINE LINE_LPS_CS
#define IMUTAG_LPS_SCK_LINE LINE_LPS_SCK
#define IMUTAG_LPS_MISO_LINE LINE_LPS_MISO
#define IMUTAG_LPS_MOSI_LINE LINE_LPS_MOSI
#define IMUTAG_LPS_DRDY_LINE LINE_LPS_DRDY

#define IMUTAG_IMU_CS_LINE LINE_LSM_CS
#define IMUTAG_IMU_SCK_LINE LINE_LSM_FLASH_SCK
#define IMUTAG_IMU_MISO_LINE LINE_LSM_FLASH_MISO
#define IMUTAG_IMU_MOSI_LINE LINE_LSM_FLASH_MOSI
#define IMUTAG_IMU_TRIGGER_LINE LINE_LSM_TRG

#define IMUTAG_BMM_DRDY_LINE LINE_BMM_INT
/** @} */

/** @name Sensor conversion constants
 * Board-level sample scale constants used by local code.
 * @{
 */
#define LINE_ACCEL_INT LINE_WKUP1
#define ACCEL_CONSTANT 0.976f
#define MAG_CONSTANT 0.01
#define SWAP_I2C 0
/** @} */

#endif
