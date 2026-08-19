/**
 * @file custom.h
 * @brief IMUTagU375 variant build constants.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef CUSTOM_H
#define CUSTOM_H

/** @name Storage and firmware identity
 * External flash sizing, strings, protocol sizing, and feature flags.
 * @{
 */
#define EXT_FLASH_SIZE (1024 * 1024 * 16)

#define FIRMWARE_STRING "IMUTagU375, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "IMUTagU375"
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
#define TAG_SPI_TRANSFER_STATUS 1
#define TAG_RTC_TRANSFER_DIAGNOSTICS 1
#define TAG_RTC_STM32U3_COMPAT 1
#define CONFIG_HAS_HIBERNATE 0
#define TAG_STATUS_FIXED_VDD100 180
/*
 * The shared storage SPI DMA helper currently supports the older STM32 DMA
 * stream/CSELR path. STM32U375 uses the DMA3/GPDMA-style ChibiOS bindings, so
 * keep external-flash log transfers on the conservative polled SPI path until
 * the U3 DMA block-transfer backend exists.
 */
#define TAG_STORAGE_SPI_DMA_BLOCK_READ 0
#define TAG_STORAGE_SPI_DMA_BLOCK_WRITE 0
/* Temporary bring-up aid: expose a retained RUNNING-state heartbeat in the
 * status debug message so detached execution can be distinguished from
 * attach/reset cursor recovery.
 */
#define TAG_RETAINED_RUN_DIAGNOSTICS 1
/* Optional logic-analyzer pulse on PA4 while building a log ACK. Leave
 * TAG_STORAGE_SPI_MEASURE_LINE disabled while using this so PA4 has one owner
 * in the trace.
 */
/* #define LOG_ACK_MEASURE_LINE LINE_IMU_TRG_TEST */
/** @} */

/** @name Sensor line aliases and conversion constants
 * Board-level wake lines and sample scale constants used by local code.
 * @{
 */
#define LINE_ACCEL_INT LINE_WKUP1
#define ACCEL_CONSTANT 0.976f
#define MAG_CONSTANT 0.01
#define SWAP_I2C 0
/** @} */

#endif
