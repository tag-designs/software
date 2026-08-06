/**
 * @file custom.h
 * @brief IMUTagNand variant build constants.
 * @author tag firmware authors
 * @date 2026-07-21
 */

#ifndef CUSTOM_H
#define CUSTOM_H

/** @name Storage and firmware identity
 * External flash sizing, strings, protocol sizing, and feature flags.
 * @{
 */
#define EXT_FLASH_SIZE (1024UL * 1024UL * 128UL)

#define FIRMWARE_STRING "IMUTagNand, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "IMUTagNand"
#define USE_LPS22HH 1
#define SENSOR_CALIBRATION 1
#define QTMONITOR_VERSION 2.0
#define PROTOBUFSIZE 4096
#define SENSOR_CONSTANTS 1
#define CALIBRATION_CONSTANTS 1
/*
 * Bring-up note: exercise Stop1 again now that the IMU trigger enables the
 * STM32U3 LPTIM2 Stop-mode clock gate. Keep short driver wait helpers on
 * their older Stop2 path while validating detached collection, because those
 * waits run inside storage and sensor transactions after monitor detach.
 */
#define USE_STOP1 1
#define USE_STOP1_DELAY 0
#define STOP1_WAKE_EXTI_GROUP1_MASK (1U << 0)
#define TAG_STM32U3_FLASH 1
#define TAG_MONITOR_RESET_RECOVERY 1
#define TAG_STOP1_WAKE_USES_INTERRUPT 1
#define TAG_CONFIGURED_IMMEDIATE_START 1
#define TAG_SPI_TRANSFER_STATUS 1
//#define TAG_RTC_TRANSFER_DIAGNOSTICS 1
#define TAG_RTC_STM32U3_COMPAT 1
#define CONFIG_HAS_HIBERNATE 0
#define TAG_STATUS_FIXED_VDD100 180
#define TAG_IMUTAG_RTC_I2C_HARDWARE 1
/* I2C1 timing for a 12.5 MHz peripheral clock, approximately 400 kHz SCL. */
#define TAG_IMUTAG_I2C_TIMINGR 0x00210D10U
/* Use DMA for external-flash block data phases; command/address/status
 * transactions remain byte-paced.
 */

 /* this is now handled automatically by configuring to use or not use chibios spi*/
#define TAG_STORAGE_SPI_DMA_BLOCK_READ 0
#define TAG_STORAGE_SPI_DMA_BLOCK_WRITE 0
#define TAG_LSM6DSV16X_FIFO_DMA_READ 0
/* Keep retained runtime diagnostics disabled during data collection. */
#define TAG_RETAINED_RUN_DIAGNOSTICS 0
/* Optional logic-analyzer pulse on PA4 while building a log ACK. Leave
 * TAG_STORAGE_SPI_MEASURE_LINE disabled while using this so PA4 has one owner
 * in the trace.
 */
/* #define LOG_ACK_MEASURE_LINE LINE_IMU_TRG_TEST */
/** @} */

/** @name Sensor line aliases and conversion constants
 * Board-level wake lines and sample scale constants used by local code.
 *
 * Maintainer note: this target keeps the U3 firmware path from
 * IMUTagU3bmm350, but uses the generated IMUTagNandv1 board pin names and the
 * GD5F SPI-NAND storage module.
 * @{
 */
#define LINE_RTC_SDA LINE_SDA
#define LINE_RTC_SCL LINE_SCL

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
#define IMUTAG_IMU_TRIGGER_LINE LINE_LMS_TRIG_2
#define IMUTAG_IMU_TRIGGER_AF 1

#define IMUTAG_BMM_DRDY_LINE LINE_BMM_INT

#define LINE_ACCEL_INT LINE_WKUP1
#define ACCEL_CONSTANT 0.976f
#define MAG_CONSTANT 0.01
#define SWAP_I2C 0
#define BMM350_I2C_ADDRESS 0x14U

/** @} */

#endif
