/**
 * @file custom.h
 * @brief IMUTagNandBmp581 variant build constants.
 * @author tag firmware authors
 * @date 2026-07-21
 */

#ifndef CUSTOM_H
#define CUSTOM_H

/** @name Storage and firmware identity
 * External flash sizing, strings, protocol sizing, and feature flags.
 * @{
 */
/** Usable SPI-NAND payload bytes after reserving the GD5F2 valid-block floor. */
#define EXT_FLASH_SIZE (2008UL * 64UL * 2048UL)

/** Firmware identity returned to host tools over the monitor protocol. */
#define FIRMWARE_STRING "IMUTagNandBmp581, Firmware version 1"
#undef  BOARD_NAME
/** Board descriptor stored in logs and reported by tag-info. */
#define BOARD_NAME "IMUTagNandBmp581"
/** Select the Bosch BMP581 pressure path in shared IMUTag code. */
#define USE_BMP581 1
/** Enable monitor-facing live magnetometer/accelerometer calibration mode. */
#define SENSOR_CALIBRATION 1
/** Minimum qtmonitor protocol/UI version expected by this target. */
#define QTMONITOR_VERSION 2.0
/** Monitor protobuf buffer size in bytes. */
#define PROTOBUFSIZE 4096
/** Report sensor conversion constants in the tag information payload. */
#define SENSOR_CONSTANTS 1
/** Enable persisted magnetometer calibration constants. */
#define CALIBRATION_CONSTANTS 1
/*
 * Bring-up note: exercise Stop1 now that the IMU trigger enables the STM32U3
 * LPTIM1 Stop-mode clock gate. Short driver waits use the U3 sleep path rather
 * than the older LPTIM1 stop-delay helper, so LPTIM1 is owned by the trigger
 * during detached collection.
 */
#define USE_STOP1 1
#define USE_STOP1_DELAY 0
#define STOP1_WAKE_EXTI_GROUP1_MASK (1U << 0)
#define TAG_STM32U3_FLASH 1
/* STM32U375xG.ld reserves a dedicated, independently erasable flash page
 * for the provisioned configuration so writeStoredConfig() can erase
 * before programming. */
#define TAG_STORED_CONFIG_OWN_PAGE 1
#define TAG_MONITOR_RESET_RECOVERY 1
#define TAG_STOP1_WAKE_USES_INTERRUPT 1
#define TAG_CONFIGURED_IMMEDIATE_START 1
#define TAG_DEFAULT_IDLE_POWER_MODE SLEEP
#define TAG_SPI_TRANSFER_STATUS 1
//#define TAG_RTC_TRANSFER_DIAGNOSTICS 1
#define TAG_RTC_STM32U3_COMPAT 1
#define TAG_RTC_REQUIRE_DIRECT_RV3028_CLKOUT 1
#define IMUTAG_USE_STM32_RTC_SMOOTH_CALIBRATION 1
#define CONFIG_HAS_HIBERNATE 0
#define TAG_STATUS_FIXED_VDD100 180
#define TAG_IMUTAG_RTC_I2C_HARDWARE 1
/* I2C1 timing for a 12.5 MHz peripheral clock, approximately 400 kHz SCL. */
#define TAG_IMUTAG_I2C_TIMINGR 0x00210D10U

/* DMA is disabled until STM32U3 SPI DMA bring-up is proven for this board. */
#define TAG_STORAGE_SPI_DMA_BLOCK_READ 0
#define TAG_STORAGE_SPI_DMA_BLOCK_WRITE 0
/* FIFO reads stay byte-paced while validating the LSM6DSV16X trigger path. */
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
 * Maintainer note: this target uses the generated IMUTagNandv2 board pin names
 * and the 2 Gbit GD5F SPI-NAND storage module.
 * @{
 */
#define LINE_RTC_SDA LINE_SDA
#define LINE_RTC_SCL LINE_SCL

/** GD5F2GM7RE SPI-NAND chip-select line. */
#define IMUTAG_FLASH_CS_LINE LINE_AT25_nCS
/** GD5F2GM7RE SPI-NAND clock line. */
#define IMUTAG_FLASH_SCK_LINE LINE_AT25_SCK
/** GD5F2GM7RE SPI-NAND MISO line. */
#define IMUTAG_FLASH_MISO_LINE LINE_AT25_MISO
/** GD5F2GM7RE SPI-NAND MOSI line. */
#define IMUTAG_FLASH_MOSI_LINE LINE_AT25_MOSI
/** Board load-switch enable for the flash rail; currently left asserted. */
#define IMUTAG_FLASH_PWR_LINE LINE_FLASH_PWR

/** BMP581 chip-select line; maps to PA10 on the breakout header. */
#define IMUTAG_LPS_CS_LINE LINE_LPS_CS
/** BMP581 SPI clock line. */
#define IMUTAG_LPS_SCK_LINE LINE_LPS_SCK
/** BMP581 SDO/MISO line. */
#define IMUTAG_LPS_MISO_LINE LINE_LPS_MISO
/** BMP581 SDI/MOSI line. */
#define IMUTAG_LPS_MOSI_LINE LINE_LPS_MOSI
/** BMP581 interrupt/data-ready line; maps to PA9 on the breakout header. */
#define IMUTAG_LPS_DRDY_LINE LINE_LPS_DRDY
/** Neutral pressure data-ready alias used by shared IMUTag collection code. */
#define IMUTAG_PRESSURE_DRDY_LINE LINE_LPS_DRDY

/** LSM6DSV16X chip-select line. */
#define IMUTAG_IMU_CS_LINE LINE_LSM_CS
/** LSM6DSV16X SPI clock line shared with the flash clock. */
#define IMUTAG_IMU_SCK_LINE LINE_AT25_SCK
/** LSM6DSV16X MISO line shared with the flash MISO net. */
#define IMUTAG_IMU_MISO_LINE LINE_AT25_MISO
/** LSM6DSV16X MOSI line shared with the flash MOSI net. */
#define IMUTAG_IMU_MOSI_LINE LINE_AT25_MOSI
/** LSM6DSV16X external ODR trigger output line. */
#define IMUTAG_IMU_TRIGGER_LINE LINE_LSM_TRG
/** Alternate-function number used by the trigger LPTIM output. */
#define IMUTAG_IMU_TRIGGER_AF 1
/** LPTIM instance number that owns the IMU trigger output. */
#define IMUTAG_IMU_TRIGGER_LPTIM_ID 1
/** LPTIM channel number routed to the IMU trigger output. */
#define IMUTAG_IMU_TRIGGER_LPTIM_CHANNEL 2

/** BMM350 interrupt/data-ready line. */
#define IMUTAG_BMM_DRDY_LINE LINE_BMM_INT

/** Legacy accelerometer interrupt alias retained for shared monitor code. */
#define LINE_ACCEL_INT LINE_WKUP1
/** Accelerometer raw-to-mg scale reported to host tools. */
#define ACCEL_CONSTANT 0.976f
/** Magnetometer raw-to-uT scale reported to host tools. */
#define MAG_CONSTANT 0.01
/** RTC/BMM350 software-I2C pins are not swapped on this board. */
#define SWAP_I2C 0
/** BMM350 7-bit I2C address used by the shared magnetometer module. */
#define BMM350_I2C_ADDRESS 0x14U

/** @} */

#endif
