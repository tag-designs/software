/*
    ChibiOS - Copyright (C) 2006..2018 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/**
 * @file    hal_flash_device.h
 * @brief   Macronix MX25Rxx35F serial flash driver header.
 *
 * @addtogroup MACRONIX_MX25Rxx35F
 * @{
 */

#ifndef HAL_FLASH_DEVICE_H
#define HAL_FLASH_DEVICE_H

#include "hal_flash.h"

/*===========================================================================*/
/* Driver constants.                                                         */
/*===========================================================================*/
/**
 * @name    Bus interface modes.
 * @{
 */
#define SNOR_BUS_DRIVER_SPI                 0U
#define SNOR_BUS_DRIVER_WSPI                1U
/** @} */
/**
 * @name    Device capabilities
 * @{
 */
#define SNOR_DEVICE_SUPPORTS_XIP            FALSE
/** @} */

/**
 * @name    Device identification
 * @{
 */
#define MX25R_SUPPORTED_MANUFACTURE_IDS      {0xC2}
#define MX25R_SUPPORTED_MEMORY_TYPE_IDS      {0x28}

/** @} */

/**
 * @name    Command codes
 * @{
 */

/* Read Operations */

#define MX25R_CMD_READ                             0x03
#define MX25R_CMD_FAST_READ                        0x0B
#define MX25R_CMD_DUAL_OUT_READ                    0x3B
#define MX25R_CMD_DUAL_INOUT_READ                  0xBB
#define MX25R_CMD_QUAD_OUT_READ                    0x6B
#define MX25R_CMD_QUAD_INOUT_READ                  0xEB

/* Program Operations */

#define MX25R_CMD_PAGE_PROG                        0x02
#define MX25R_CMD_QUAD_PAGE_PROG                   0x38

/* Erase Operations */

#define MX25R_CMD_SECTOR_ERASE                     0x20
#define MX25R_CMD_SUBBLOCK_ERASE                   0x52
#define MX25R_CMD_BLOCK_ERASE                      0xD8
#define MX25R_CMD_CHIP_ERASE                       0x60
#define MX25R_CHIP_ERASE_CMD_2                     0xC7

#define MX25R_CMD_PROG_ERASE_RESUME                0x7A
#define MX25R_CMD_PROG_ERASE_RESUME_2              0x30
#define MX25R_CMD_PROG_ERASE_SUSPEND               0x75
#define MX25R_CMD_PROG_ERASE_SUSPEND_2             0xB0

/* Identification Operations */

#define MX25R_CMD_READ_ID                          0x9F
#define MX25R_CMD_READ_ELECTRONIC_ID               0xAB
#define MX25R_CMD_READ_ELEC_MANUFACTURER_DEVICE_ID 0x90
#define MX25R_CMD_READ_SERIAL_FLASH_DISCO_PARAM    0x5A

/* Write Operations */

#define MX25R_CMD_WRITE_ENABLE                     0x06
#define MX25R_CMD_WRITE_DISABLE                    0x04

/* Register Operations */

#define MX25R_CMD_READ_STATUS_REG                  0x05
#define MX25R_CMD_READ_CFG_REG                     0x15
#define MX25R_CMD_WRITE_STATUS_CFG_REG             0x01

#define MX25R_CMD_READ_SEC_REG                     0x2B
#define MX25R_CMD_WRITE_SEC_REG                    0x2F

/* Power Down Operations */

#define MX25R_CMD_DEEP_POWER_DOWN                  0xB9

/* Burst Operations */

#define MX25R_CMD_SET_BURST_LENGTH                 0xC0

/* One-Time Programmable Operations */

#define MX25R_CMD_ENTER_SECURED_OTP                0xB1
#define MX25R_CMD_EXIT_SECURED_OTP                 0xC1

/* No Operation */

#define MX25R_CMD_NO_OPERATION                     0x00

/* Reset Operations */

#define MX25R_CMD_RESET_ENABLE                     0x66
#define MX25R_CMD_RESET_MEMORY                     0x99
#define MX25R_CMD_RELEASE_READ_ENHANCED            0xFF
/** @} */


/**
 * @name    Flags and register bits
 * @{
 */

/* Status Register */

#define MX25R_FLAGS_SR_WIP                    ((uint8_t)0x01)    /*!< Write in progress */
#define MX25R_FLAGS_SR_WEL                    ((uint8_t)0x02)    /*!< Write enable latch */
#define MX25R_FLAGS_SR_BP                     ((uint8_t)0x3C)    /*!< Block protect */
#define MX25R_FLAGS_SR_QE                     ((uint8_t)0x40)    /*!< Quad enable */
#define MX25R_FLAGS_SR_SRWD                   ((uint8_t)0x80)    /*!< Status register write disable */

/* Configuration Register 1 */

#define MX25R_FLAGS_CR1_TB                    ((uint8_t)0x08)    /*!< Top / bottom */

/* Configuration Register 2 */

#define MX25R_FLAGS_CR2_LH_SWITCH             ((uint8_t)0x02)    /*!< Low power / high performance switch */

/* Security Register */

#define MX25R_FLAGS_SECR_SOI                  ((uint8_t)0x01)    /*!< Secured OTP indicator */
#define MX25R_FLAGS_SECR_LDSO                 ((uint8_t)0x02)    /*!< Lock-down secured OTP */
#define MX25R_FLAGS_SECR_PSB                  ((uint8_t)0x04)    /*!< Program suspend bit */
#define MX25R_FLAGS_SECR_ESB                  ((uint8_t)0x08)    /*!< Erase suspend bit */
#define MX25R_FLAGS_SECR_P_FAIL               ((uint8_t)0x20)    /*!< Program fail flag */
#define MX25R_FLAGS_SECR_E_FAIL               ((uint8_t)0x40)    /*!< Erase fail flag */

/* Flags */

#define MX25R_FLAGS_PROGRAM_ERASE                    0x80U
#define MX25R_FLAGS_ERASE_SUSPEND                    0x40U
#define MX25R_FLAGS_ERASE_ERROR                      0x20U
#define MX25R_FLAGS_PROGRAM_ERROR                    0x10U
#define MX25R_FLAGS_VPP_ERROR                        0x08U
#define MX25R_FLAGS_PROGRAM_SUSPEND                  0x04U
#define MX25R_FLAGS_PROTECTION_ERROR                 0x02U
#define MX25R_FLAGS_RESERVED                         0x01U
#define MX25R_FLAGS_ALL_ERRORS                   (MX25R_FLAGS_ERASE_ERROR |   \
                                                 MX25R_FLAGS_PROGRAM_ERROR | \
                                                 MX25R_FLAGS_VPP_ERROR |     \
                                                 MX25R_FLAGS_PROTECTION_ERROR)
/** @} */

/**
 * @name    Bus interface modes.
 * @{
 */

#define MX25R_BUS_MODE_WSPI1L                1U
#define MX25R_BUS_MODE_WSPI2L                2U
#define MX25R_BUS_MODE_WSPI4L                4U

/** @} */


/**
 * @name    Driver Constants
 * @{
 */

#define MX25R_BUS_DUMMY_1L                   0U
#define MX25R_BUS_DUMMY_2L                   4U
#define MX25R_BUS_DUMMY_4L                   6U
/** @} */

/*===========================================================================*/
/* Driver pre-compile time settings.                                         */
/*===========================================================================*/

/**
 * @brief   Switch WSPI bus width on initialization.
 * @details A bus width initialization is performed by writing the
 *          Enhanced Volatile Configuration Register. If the flash
 *          device is configured using the Non Volatile Configuration
 *          Register then this option is not required.
 * @note    This option is only valid in WSPI bus mode.
 */
#if !defined(MX25R_SWITCH_WIDTH) || defined(__DOXYGEN__)
#define MX25R_SWITCH_WIDTH                   TRUE
#endif

/**
 * @brief   Device bus mode to be used.
 * #note    if @p MX25R_SWITCH_WIDTH is @p FALSE then this is the bus mode
 *          that the device is expected to be using.
 * #note    if @p MX25R_SWITCH_WIDTH is @p TRUE then this is the bus mode
 *          that the device will be switched in.
 * @note    This option is only valid in WSPI bus mode.
 * @note    The polarity of the WP# signal is inconsistent
 *          with the stm32 wspi device which forces it low on
 *          1 line and 2 line transactions. 
 *          In order to use 1-line and 2-line modes
 *          the QSPI IO2 and IO2 lines must either be forced high or left floating
 */
#if !defined(MX25R_BUS_MODE) || defined(__DOXYGEN__)
#define MX25R_BUS_MODE MX25R_BUS_MODE_WSPI4L
#endif

/**
 * @brief   Choose Power mode
 */

#if !defined(MX25R_HIGH_PERFORMANCE_MODE) || defined(__DOXYGEN__)
#define MX25R_HIGH_PERFORMANCE_MODE   FALSE
#endif

/**
 * @brief   Delays insertions.
 * @details If enabled this options inserts delays into the flash waiting
 *          routines releasing some extra CPU time for threads with lower
 *          priority, this may slow down the driver a bit however.
 */
#if !defined(MX25R_NICE_WAITING) || defined(__DOXYGEN__)
#define MX25R_NICE_WAITING                   TRUE
#endif

/**
 * @brief   Uses 4kB sub-sectors rather than 64kB sectors.
 */
#if !defined(MX25R_USE_SUB_SECTORS) || defined(__DOXYGEN__)
#define MX25R_USE_SUB_SECTORS                FALSE
#endif

/**
 * @brief   Size of the compare buffer.
 * @details This buffer is allocated in the stack frame of the function
 *          @p flashVerifyErase() and its size must be a power of two.
 *          Larger buffers lead to better verify performance but increase
 *          stack usage for that function.
 */
#if !defined(MX25R_COMPARE_BUFFER_SIZE) || defined(__DOXYGEN__)
#define MX25R_COMPARE_BUFFER_SIZE            32
#endif

#if !defined(MX25R_RESET_ON_INIT) || defined(__DOXYGEN__)
#define MX25R_RESET_ON_INIT FALSE
#endif

/*===========================================================================*/
/* Derived constants and error checks.                                       */
/*===========================================================================*/

#if (MX25R_COMPARE_BUFFER_SIZE & (MX25R_COMPARE_BUFFER_SIZE - 1)) != 0
#error "invalid MX25R_COMPARE_BUFFER_SIZE value"
#endif


/**
 * @brief   Settings for command only.   Only Read/Program are multiline 
 *                                       and handled separately
 */

#define SNOR_WSPI_CFG_CMD               (WSPI_CFG_CMD_MODE_ONE_LINE       | \
                                         WSPI_CFG_ADDR_MODE_NONE          | \
                                         WSPI_CFG_ALT_MODE_NONE           | \
                                         WSPI_CFG_DATA_MODE_NONE          | \
                                         WSPI_CFG_CMD_SIZE_8              | \
                                         WSPI_CFG_ADDR_SIZE_24)

#define SNOR_WSPI_CFG_CMD_ADDR          (WSPI_CFG_CMD_MODE_ONE_LINE       | \
                                         WSPI_CFG_ADDR_MODE_ONE_LINE      | \
                                         WSPI_CFG_ALT_MODE_NONE           | \
                                         WSPI_CFG_DATA_MODE_NONE          | \
                                         WSPI_CFG_CMD_SIZE_8              | \
                                         WSPI_CFG_ADDR_SIZE_24)

#define SNOR_WSPI_CFG_CMD_DATA          (WSPI_CFG_CMD_MODE_ONE_LINE       | \
                                         WSPI_CFG_ADDR_MODE_NONE          | \
                                         WSPI_CFG_ALT_MODE_NONE           | \
                                         WSPI_CFG_DATA_MODE_ONE_LINE      | \
                                         WSPI_CFG_CMD_SIZE_8              | \
                                         WSPI_CFG_ADDR_SIZE_24)

#define SNOR_WSPI_CFG_CMD_ADDR_DATA     (WSPI_CFG_CMD_MODE_ONE_LINE       | \
                                         WSPI_CFG_ADDR_MODE_ONE_LINE      | \
                                         WSPI_CFG_ALT_MODE_NONE           | \
                                         WSPI_CFG_DATA_MODE_ONE_LINE      | \
                                         WSPI_CFG_CMD_SIZE_8              | \
                                         WSPI_CFG_ADDR_SIZE_24)

/*===========================================================================*/
/* Driver data structures and types.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Driver macros.                                                            */
/*===========================================================================*/

/*===========================================================================*/
/* External declarations.                                                    */
/*===========================================================================*/

#if !defined(__DOXYGEN__)
extern flash_descriptor_t snor_descriptor;
#endif

#if !defined(SNOR_BUS_DRIVER)
#define SNOR_BUS_DRIVER SNOR_BUS_DRIVER_WSPI
#endif

#if (SNOR_BUS_DRIVER == SNOR_BUS_DRIVER_WSPI) && (WSPI_SUPPORTS_MEMMAP == TRUE)
extern const wspi_command_t snor_memmap_read;
#endif

#ifdef __cplusplus
extern "C" {
#endif
  void snor_device_init(SNORDriver *devp);
  flash_error_t snor_device_read(SNORDriver *devp, flash_offset_t offset,
                                 size_t n, uint8_t *rp);
  flash_error_t snor_device_program(SNORDriver *devp, flash_offset_t offset,
                                    size_t n, const uint8_t *pp);
  flash_error_t snor_device_start_erase_all(SNORDriver *devp);
  flash_error_t snor_device_start_erase_sector(SNORDriver *devp,
                                               flash_sector_t sector);
  flash_error_t snor_device_verify_erase(SNORDriver *devp,
                                         flash_sector_t sector);
  flash_error_t snor_device_query_erase(SNORDriver *devp, uint32_t *msec);
  flash_error_t snor_device_read_sfdp(SNORDriver *devp, flash_offset_t offset,
                                      size_t n, uint8_t *rp);

  flash_error_t snor_device_power_down(SNORDriver *devp);
  flash_error_t snor_device_power_up(SNORDriver *devp);
#ifdef __cplusplus
}
#endif

#endif /* HAL_FLASH_DEVICE_H */

/** @} */

