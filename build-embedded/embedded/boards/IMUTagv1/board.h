/*
    ChibiOS - Copyright (C) 2006-2026 Giovanni Di Sirio.

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

/*
 * This file has been automatically generated using ChibiStudio board
 * generator plugin. Do not edit manually.
 */

#ifndef BOARD_H
#define BOARD_H

/*===========================================================================*/
/* Driver constants.                                                         */
/*===========================================================================*/

/*
 * Setup for IMUTag v1 board.
 */

/*
 * Board identifier.
 */
#define BOARD_IMUTagv1
#define BOARD_NAME                  "IMUTag v1"

/*
 * Board oscillators-related settings.
 * NOTE: HSE not fitted.
 */
#if !defined(STM32_LSECLK)
#define STM32_LSECLK                32768U
#endif

#define STM32_LSEDRV                (0U << 3U)

#if !defined(STM32_HSECLK)
#define STM32_HSECLK                0U
#endif

/*
 * Board voltages.
 * Required for performance limits calculation.
 */
#define STM32_VDD                   300U

/*
 * MCU type as defined in the ST header.
 */
#undef STM32L432xx
#define STM32L432xx

/*
 * IO pins assignments.
 */
#define GPIOA_WKUP1                 0U
#define GPIOA_LED1                  1U
#define GPIOA_MAG_TRG               2U
#define GPIOA_MAG_CS                3U
#define GPIOA_IMU_TRG_TEST          4U
#define GPIOA_ACCEL_SCK             5U
#define GPIOA_ACCEL_MISO            6U
#define GPIOA_ACCEL_MOSI            7U
#define GPIOA_ACCEL_TRG             8U
#define GPIOA_LPS_DRDY              9U
#define GPIOA_LPS_CS                10U
#define GPIOA_LPS_MISO              11U
#define GPIOA_LPS_MOSI              12U
#define GPIOA_SWDIO                 13U
#define GPIOA_SWCLK                 14U
#define GPIOA_FLASH_nCS             15U

#define GPIOB_ACCEL_CS              0U
#define GPIOB_LED2                  1U
#define GPIOB_BOOT1                 2U
#define GPIOB_FLASH_SCK             3U
#define GPIOB_FLASH_MISO            4U
#define GPIOB_FLASH_MOSI            5U
#define GPIOB_RTC_SDA               6U
#define GPIOB_RTC_SCL               7U
#define GPIOB_PIN8                  8U
#define GPIOB_PIN9                  9U
#define GPIOB_PIN10                 10U
#define GPIOB_PIN11                 11U
#define GPIOB_PIN12                 12U
#define GPIOB_PIN13                 13U
#define GPIOB_PIN14                 14U
#define GPIOB_PIN15                 15U

#define GPIOC_PIN0                  0U
#define GPIOC_PIN1                  1U
#define GPIOC_PIN2                  2U
#define GPIOC_PIN3                  3U
#define GPIOC_PIN4                  4U
#define GPIOC_PIN5                  5U
#define GPIOC_PIN6                  6U
#define GPIOC_PIN7                  7U
#define GPIOC_PIN8                  8U
#define GPIOC_PIN9                  9U
#define GPIOC_PIN10                 10U
#define GPIOC_PIN11                 11U
#define GPIOC_PIN12                 12U
#define GPIOC_PIN13                 13U
#define GPIOC_CLKIN                 14U
#define GPIOC_OSC32_OUT             15U

#define GPIOD_PIN0                  0U
#define GPIOD_PIN1                  1U
#define GPIOD_PIN2                  2U
#define GPIOD_PIN3                  3U
#define GPIOD_PIN4                  4U
#define GPIOD_PIN5                  5U
#define GPIOD_PIN6                  6U
#define GPIOD_PIN7                  7U
#define GPIOD_PIN8                  8U
#define GPIOD_PIN9                  9U
#define GPIOD_PIN10                 10U
#define GPIOD_PIN11                 11U
#define GPIOD_PIN12                 12U
#define GPIOD_PIN13                 13U
#define GPIOD_PIN14                 14U
#define GPIOD_PIN15                 15U

#define GPIOE_PIN0                  0U
#define GPIOE_PIN1                  1U
#define GPIOE_PIN2                  2U
#define GPIOE_PIN3                  3U
#define GPIOE_PIN4                  4U
#define GPIOE_PIN5                  5U
#define GPIOE_PIN6                  6U
#define GPIOE_PIN7                  7U
#define GPIOE_PIN8                  8U
#define GPIOE_PIN9                  9U
#define GPIOE_PIN10                 10U
#define GPIOE_PIN11                 11U
#define GPIOE_PIN12                 12U
#define GPIOE_PIN13                 13U
#define GPIOE_PIN14                 14U
#define GPIOE_PIN15                 15U

#define GPIOF_PIN0                  0U
#define GPIOF_PIN1                  1U
#define GPIOF_PIN2                  2U
#define GPIOF_PIN3                  3U
#define GPIOF_PIN4                  4U
#define GPIOF_PIN5                  5U
#define GPIOF_PIN6                  6U
#define GPIOF_PIN7                  7U
#define GPIOF_PIN8                  8U
#define GPIOF_PIN9                  9U
#define GPIOF_PIN10                 10U
#define GPIOF_PIN11                 11U
#define GPIOF_PIN12                 12U
#define GPIOF_PIN13                 13U
#define GPIOF_PIN14                 14U
#define GPIOF_PIN15                 15U

#define GPIOG_PIN0                  0U
#define GPIOG_PIN1                  1U
#define GPIOG_PIN2                  2U
#define GPIOG_PIN3                  3U
#define GPIOG_PIN4                  4U
#define GPIOG_PIN5                  5U
#define GPIOG_PIN6                  6U
#define GPIOG_PIN7                  7U
#define GPIOG_PIN8                  8U
#define GPIOG_PIN9                  9U
#define GPIOG_PIN10                 10U
#define GPIOG_PIN11                 11U
#define GPIOG_PIN12                 12U
#define GPIOG_PIN13                 13U
#define GPIOG_PIN14                 14U
#define GPIOG_PIN15                 15U

#define GPIOH_OSC_IN                0U
#define GPIOH_OSC_OUT               1U
#define GPIOH_PIN2                  2U
#define GPIOH_PIN3                  3U
#define GPIOH_PIN4                  4U
#define GPIOH_PIN5                  5U
#define GPIOH_PIN6                  6U
#define GPIOH_PIN7                  7U
#define GPIOH_PIN8                  8U
#define GPIOH_PIN9                  9U
#define GPIOH_PIN10                 10U
#define GPIOH_PIN11                 11U
#define GPIOH_PIN12                 12U
#define GPIOH_PIN13                 13U
#define GPIOH_PIN14                 14U
#define GPIOH_PIN15                 15U

/*
 * IO lines assignments.
 */
#define LINE_WKUP1                  PAL_LINE(GPIOA, 0U)
#define LINE_LED1                   PAL_LINE(GPIOA, 1U)
#define LINE_MAG_TRG                PAL_LINE(GPIOA, 2U)
#define LINE_MAG_CS                 PAL_LINE(GPIOA, 3U)
#define LINE_IMU_TRG_TEST           PAL_LINE(GPIOA, 4U)
#define LINE_ACCEL_SCK              PAL_LINE(GPIOA, 5U)
#define LINE_ACCEL_MISO             PAL_LINE(GPIOA, 6U)
#define LINE_ACCEL_MOSI             PAL_LINE(GPIOA, 7U)
#define LINE_ACCEL_TRG              PAL_LINE(GPIOA, 8U)
#define LINE_LPS_DRDY               PAL_LINE(GPIOA, 9U)
#define LINE_LPS_CS                 PAL_LINE(GPIOA, 10U)
#define LINE_LPS_MISO               PAL_LINE(GPIOA, 11U)
#define LINE_LPS_MOSI               PAL_LINE(GPIOA, 12U)
#define LINE_SWDIO                  PAL_LINE(GPIOA, 13U)
#define LINE_SWCLK                  PAL_LINE(GPIOA, 14U)
#define LINE_FLASH_nCS              PAL_LINE(GPIOA, 15U)
#define LINE_ACCEL_CS               PAL_LINE(GPIOB, 0U)
#define LINE_LED2                   PAL_LINE(GPIOB, 1U)
#define LINE_BOOT1                  PAL_LINE(GPIOB, 2U)
#define LINE_FLASH_SCK              PAL_LINE(GPIOB, 3U)
#define LINE_FLASH_MISO             PAL_LINE(GPIOB, 4U)
#define LINE_FLASH_MOSI             PAL_LINE(GPIOB, 5U)
#define LINE_RTC_SDA                PAL_LINE(GPIOB, 6U)
#define LINE_RTC_SCL                PAL_LINE(GPIOB, 7U)
#define LINE_CLKIN                  PAL_LINE(GPIOC, 14U)
#define LINE_OSC32_OUT              PAL_LINE(GPIOC, 15U)
#define LINE_OSC_IN                 PAL_LINE(GPIOH, 0U)
#define LINE_OSC_OUT                PAL_LINE(GPIOH, 1U)

/*===========================================================================*/
/* Driver pre-compile time settings.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Derived constants and error checks.                                       */
/*===========================================================================*/

/*===========================================================================*/
/* Driver data structures and types.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Driver macros.                                                            */
/*===========================================================================*/

/*
 * I/O ports initial setup, this configuration is established soon after reset
 * in the initialization code.
 * Please refer to the STM32 Reference Manual for details.
 */
#define PIN_MODE_INPUT(n)           (0U << ((n) * 2U))
#define PIN_MODE_OUTPUT(n)          (1U << ((n) * 2U))
#define PIN_MODE_ALTERNATE(n)       (2U << ((n) * 2U))
#define PIN_MODE_ANALOG(n)          (3U << ((n) * 2U))
#define PIN_ODR_LOW(n)              (0U << (n))
#define PIN_ODR_HIGH(n)             (1U << (n))
#define PIN_OTYPE_PUSHPULL(n)       (0U << (n))
#define PIN_OTYPE_OPENDRAIN(n)      (1U << (n))
#define PIN_OSPEED_VERYLOW(n)       (0U << ((n) * 2U))
#define PIN_OSPEED_LOW(n)           (1U << ((n) * 2U))
#define PIN_OSPEED_MEDIUM(n)        (2U << ((n) * 2U))
#define PIN_OSPEED_HIGH(n)          (3U << ((n) * 2U))
#define PIN_PUPDR_FLOATING(n)       (0U << ((n) * 2U))
#define PIN_PUPDR_PULLUP(n)         (1U << ((n) * 2U))
#define PIN_PUPDR_PULLDOWN(n)       (2U << ((n) * 2U))
#define PIN_AFIO_AF(n, v)           ((v) << (((n) % 8U) * 4U))
#define PIN_ASCR_DISABLED(n)        (0U << (n))
#define PIN_ASCR_ENABLED(n)         (1U << (n))
#define PIN_LOCKR_DISABLED(n)       (0U << (n))
#define PIN_LOCKR_ENABLED(n)        (1U << (n))

/*
 * GPIOA setup:
 *
 * PA0  - WKUP1                     (input floating).
 * PA1  - LED1                      (analog).
 * PA2  - MAG_TRG                   (analog).
 * PA3  - MAG_CS                    (output pushpull high).
 * PA4  - IMU_TRG_TEST              (analog).
 * PA5  - ACCEL_SCK                 (output pushpull maximum).
 * PA6  - ACCEL_MISO                (analog).
 * PA7  - ACCEL_MOSI                (output pushpull maximum).
 * PA8  - ACCEL_TRG                 (analog).
 * PA9  - LPS_DRDY                  (analog).
 * PA10 - LPS_CS                    (output pushpull maximum).
 * PA11 - LPS_MISO                  (analog).
 * PA12 - LPS_MOSI                  (output pushpull maximum).
 * PA13 - SWDIO                     (alternate 0).
 * PA14 - SWCLK                     (alternate 0).
 * PA15 - FLASH_nCS                 (output pushpull low).
 */
#define VAL_GPIOA_MODER             (PIN_MODE_INPUT(GPIOA_WKUP1) |          \
                                     PIN_MODE_ANALOG(GPIOA_LED1) |          \
                                     PIN_MODE_ANALOG(GPIOA_MAG_TRG) |       \
                                     PIN_MODE_OUTPUT(GPIOA_MAG_CS) |        \
                                     PIN_MODE_ANALOG(GPIOA_IMU_TRG_TEST) |  \
                                     PIN_MODE_OUTPUT(GPIOA_ACCEL_SCK) |     \
                                     PIN_MODE_ANALOG(GPIOA_ACCEL_MISO) |    \
                                     PIN_MODE_OUTPUT(GPIOA_ACCEL_MOSI) |    \
                                     PIN_MODE_ANALOG(GPIOA_ACCEL_TRG) |     \
                                     PIN_MODE_ANALOG(GPIOA_LPS_DRDY) |      \
                                     PIN_MODE_OUTPUT(GPIOA_LPS_CS) |        \
                                     PIN_MODE_ANALOG(GPIOA_LPS_MISO) |      \
                                     PIN_MODE_OUTPUT(GPIOA_LPS_MOSI) |      \
                                     PIN_MODE_ALTERNATE(GPIOA_SWDIO) |      \
                                     PIN_MODE_ALTERNATE(GPIOA_SWCLK) |      \
                                     PIN_MODE_OUTPUT(GPIOA_FLASH_nCS))
#define VAL_GPIOA_OTYPER            (PIN_OTYPE_PUSHPULL(GPIOA_WKUP1) |      \
                                     PIN_OTYPE_OPENDRAIN(GPIOA_LED1) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOA_MAG_TRG) |    \
                                     PIN_OTYPE_PUSHPULL(GPIOA_MAG_CS) |     \
                                     PIN_OTYPE_PUSHPULL(GPIOA_IMU_TRG_TEST) |\
                                     PIN_OTYPE_PUSHPULL(GPIOA_ACCEL_SCK) |  \
                                     PIN_OTYPE_PUSHPULL(GPIOA_ACCEL_MISO) | \
                                     PIN_OTYPE_PUSHPULL(GPIOA_ACCEL_MOSI) | \
                                     PIN_OTYPE_PUSHPULL(GPIOA_ACCEL_TRG) |  \
                                     PIN_OTYPE_PUSHPULL(GPIOA_LPS_DRDY) |   \
                                     PIN_OTYPE_PUSHPULL(GPIOA_LPS_CS) |     \
                                     PIN_OTYPE_PUSHPULL(GPIOA_LPS_MISO) |   \
                                     PIN_OTYPE_PUSHPULL(GPIOA_LPS_MOSI) |   \
                                     PIN_OTYPE_PUSHPULL(GPIOA_SWDIO) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOA_SWCLK) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOA_FLASH_nCS))
#define VAL_GPIOA_OSPEEDR           (PIN_OSPEED_LOW(GPIOA_WKUP1) |          \
                                     PIN_OSPEED_LOW(GPIOA_LED1) |           \
                                     PIN_OSPEED_HIGH(GPIOA_MAG_TRG) |       \
                                     PIN_OSPEED_MEDIUM(GPIOA_MAG_CS) |      \
                                     PIN_OSPEED_HIGH(GPIOA_IMU_TRG_TEST) |  \
                                     PIN_OSPEED_HIGH(GPIOA_ACCEL_SCK) |     \
                                     PIN_OSPEED_HIGH(GPIOA_ACCEL_MISO) |    \
                                     PIN_OSPEED_HIGH(GPIOA_ACCEL_MOSI) |    \
                                     PIN_OSPEED_HIGH(GPIOA_ACCEL_TRG) |     \
                                     PIN_OSPEED_LOW(GPIOA_LPS_DRDY) |       \
                                     PIN_OSPEED_HIGH(GPIOA_LPS_CS) |        \
                                     PIN_OSPEED_HIGH(GPIOA_LPS_MISO) |      \
                                     PIN_OSPEED_HIGH(GPIOA_LPS_MOSI) |      \
                                     PIN_OSPEED_HIGH(GPIOA_SWDIO) |         \
                                     PIN_OSPEED_HIGH(GPIOA_SWCLK) |         \
                                     PIN_OSPEED_LOW(GPIOA_FLASH_nCS))
#define VAL_GPIOA_PUPDR             (PIN_PUPDR_FLOATING(GPIOA_WKUP1) |      \
                                     PIN_PUPDR_FLOATING(GPIOA_LED1) |       \
                                     PIN_PUPDR_FLOATING(GPIOA_MAG_TRG) |    \
                                     PIN_PUPDR_FLOATING(GPIOA_MAG_CS) |     \
                                     PIN_PUPDR_FLOATING(GPIOA_IMU_TRG_TEST) |\
                                     PIN_PUPDR_FLOATING(GPIOA_ACCEL_SCK) |  \
                                     PIN_PUPDR_FLOATING(GPIOA_ACCEL_MISO) | \
                                     PIN_PUPDR_FLOATING(GPIOA_ACCEL_MOSI) | \
                                     PIN_PUPDR_FLOATING(GPIOA_ACCEL_TRG) |  \
                                     PIN_PUPDR_FLOATING(GPIOA_LPS_DRDY) |   \
                                     PIN_PUPDR_FLOATING(GPIOA_LPS_CS) |     \
                                     PIN_PUPDR_FLOATING(GPIOA_LPS_MISO) |   \
                                     PIN_PUPDR_FLOATING(GPIOA_LPS_MOSI) |   \
                                     PIN_PUPDR_PULLUP(GPIOA_SWDIO) |        \
                                     PIN_PUPDR_PULLDOWN(GPIOA_SWCLK) |      \
                                     PIN_PUPDR_FLOATING(GPIOA_FLASH_nCS))
#define VAL_GPIOA_ODR               (PIN_ODR_HIGH(GPIOA_WKUP1) |            \
                                     PIN_ODR_HIGH(GPIOA_LED1) |             \
                                     PIN_ODR_HIGH(GPIOA_MAG_TRG) |          \
                                     PIN_ODR_HIGH(GPIOA_MAG_CS) |           \
                                     PIN_ODR_LOW(GPIOA_IMU_TRG_TEST) |      \
                                     PIN_ODR_LOW(GPIOA_ACCEL_SCK) |         \
                                     PIN_ODR_LOW(GPIOA_ACCEL_MISO) |        \
                                     PIN_ODR_LOW(GPIOA_ACCEL_MOSI) |        \
                                     PIN_ODR_LOW(GPIOA_ACCEL_TRG) |         \
                                     PIN_ODR_LOW(GPIOA_LPS_DRDY) |          \
                                     PIN_ODR_HIGH(GPIOA_LPS_CS) |           \
                                     PIN_ODR_LOW(GPIOA_LPS_MISO) |          \
                                     PIN_ODR_LOW(GPIOA_LPS_MOSI) |          \
                                     PIN_ODR_HIGH(GPIOA_SWDIO) |            \
                                     PIN_ODR_HIGH(GPIOA_SWCLK) |            \
                                     PIN_ODR_HIGH(GPIOA_FLASH_nCS))
#define VAL_GPIOA_AFRL              (PIN_AFIO_AF(GPIOA_WKUP1, 0U) |         \
                                     PIN_AFIO_AF(GPIOA_LED1, 0U) |          \
                                     PIN_AFIO_AF(GPIOA_MAG_TRG, 0U) |       \
                                     PIN_AFIO_AF(GPIOA_MAG_CS, 0U) |        \
                                     PIN_AFIO_AF(GPIOA_IMU_TRG_TEST, 14U) | \
                                     PIN_AFIO_AF(GPIOA_ACCEL_SCK, 5U) |     \
                                     PIN_AFIO_AF(GPIOA_ACCEL_MISO, 5U) |    \
                                     PIN_AFIO_AF(GPIOA_ACCEL_MOSI, 5U))
#define VAL_GPIOA_AFRH              (PIN_AFIO_AF(GPIOA_ACCEL_TRG, 14U) |    \
                                     PIN_AFIO_AF(GPIOA_LPS_DRDY, 0U) |      \
                                     PIN_AFIO_AF(GPIOA_LPS_CS, 0U) |        \
                                     PIN_AFIO_AF(GPIOA_LPS_MISO, 5U) |      \
                                     PIN_AFIO_AF(GPIOA_LPS_MOSI, 5U) |      \
                                     PIN_AFIO_AF(GPIOA_SWDIO, 0U) |         \
                                     PIN_AFIO_AF(GPIOA_SWCLK, 0U) |         \
                                     PIN_AFIO_AF(GPIOA_FLASH_nCS, 0U))
#define VAL_GPIOA_ASCR              (PIN_ASCR_DISABLED(GPIOA_WKUP1) |       \
                                     PIN_ASCR_DISABLED(GPIOA_LED1) |        \
                                     PIN_ASCR_DISABLED(GPIOA_MAG_TRG) |     \
                                     PIN_ASCR_DISABLED(GPIOA_MAG_CS) |      \
                                     PIN_ASCR_DISABLED(GPIOA_IMU_TRG_TEST) |\
                                     PIN_ASCR_DISABLED(GPIOA_ACCEL_SCK) |   \
                                     PIN_ASCR_DISABLED(GPIOA_ACCEL_MISO) |  \
                                     PIN_ASCR_DISABLED(GPIOA_ACCEL_MOSI) |  \
                                     PIN_ASCR_DISABLED(GPIOA_ACCEL_TRG) |   \
                                     PIN_ASCR_DISABLED(GPIOA_LPS_DRDY) |    \
                                     PIN_ASCR_DISABLED(GPIOA_LPS_CS) |      \
                                     PIN_ASCR_DISABLED(GPIOA_LPS_MISO) |    \
                                     PIN_ASCR_DISABLED(GPIOA_LPS_MOSI) |    \
                                     PIN_ASCR_DISABLED(GPIOA_SWDIO) |       \
                                     PIN_ASCR_DISABLED(GPIOA_SWCLK) |       \
                                     PIN_ASCR_DISABLED(GPIOA_FLASH_nCS))
#define VAL_GPIOA_LOCKR             (PIN_LOCKR_DISABLED(GPIOA_WKUP1) |      \
                                     PIN_LOCKR_DISABLED(GPIOA_LED1) |       \
                                     PIN_LOCKR_DISABLED(GPIOA_MAG_TRG) |    \
                                     PIN_LOCKR_DISABLED(GPIOA_MAG_CS) |     \
                                     PIN_LOCKR_DISABLED(GPIOA_IMU_TRG_TEST) |\
                                     PIN_LOCKR_DISABLED(GPIOA_ACCEL_SCK) |  \
                                     PIN_LOCKR_DISABLED(GPIOA_ACCEL_MISO) | \
                                     PIN_LOCKR_DISABLED(GPIOA_ACCEL_MOSI) | \
                                     PIN_LOCKR_DISABLED(GPIOA_ACCEL_TRG) |  \
                                     PIN_LOCKR_DISABLED(GPIOA_LPS_DRDY) |   \
                                     PIN_LOCKR_DISABLED(GPIOA_LPS_CS) |     \
                                     PIN_LOCKR_DISABLED(GPIOA_LPS_MISO) |   \
                                     PIN_LOCKR_DISABLED(GPIOA_LPS_MOSI) |   \
                                     PIN_LOCKR_DISABLED(GPIOA_SWDIO) |      \
                                     PIN_LOCKR_DISABLED(GPIOA_SWCLK) |      \
                                     PIN_LOCKR_DISABLED(GPIOA_FLASH_nCS))

/*
 * GPIOB setup:
 *
 * PB0  - ACCEL_CS                  (output pushpull low).
 * PB1  - LED2                      (analog).
 * PB2  - BOOT1                     (analog).
 * PB3  - FLASH_SCK                 (output pushpull maximum).
 * PB4  - FLASH_MISO                (analog).
 * PB5  - FLASH_MOSI                (output pushpull high).
 * PB6  - RTC_SDA                   (output opendrain maximum).
 * PB7  - RTC_SCL                   (output opendrain maximum).
 * PB8  - PIN8                      (analog).
 * PB9  - PIN9                      (analog).
 * PB10 - PIN10                     (analog).
 * PB11 - PIN11                     (analog).
 * PB12 - PIN12                     (analog).
 * PB13 - PIN13                     (analog).
 * PB14 - PIN14                     (analog).
 * PB15 - PIN15                     (analog).
 */
#define VAL_GPIOB_MODER             (PIN_MODE_OUTPUT(GPIOB_ACCEL_CS) |      \
                                     PIN_MODE_ANALOG(GPIOB_LED2) |          \
                                     PIN_MODE_ANALOG(GPIOB_BOOT1) |         \
                                     PIN_MODE_OUTPUT(GPIOB_FLASH_SCK) |     \
                                     PIN_MODE_ANALOG(GPIOB_FLASH_MISO) |    \
                                     PIN_MODE_OUTPUT(GPIOB_FLASH_MOSI) |    \
                                     PIN_MODE_OUTPUT(GPIOB_RTC_SDA) |       \
                                     PIN_MODE_OUTPUT(GPIOB_RTC_SCL) |       \
                                     PIN_MODE_ANALOG(GPIOB_PIN8) |          \
                                     PIN_MODE_ANALOG(GPIOB_PIN9) |          \
                                     PIN_MODE_ANALOG(GPIOB_PIN10) |         \
                                     PIN_MODE_ANALOG(GPIOB_PIN11) |         \
                                     PIN_MODE_ANALOG(GPIOB_PIN12) |         \
                                     PIN_MODE_ANALOG(GPIOB_PIN13) |         \
                                     PIN_MODE_ANALOG(GPIOB_PIN14) |         \
                                     PIN_MODE_ANALOG(GPIOB_PIN15))
#define VAL_GPIOB_OTYPER            (PIN_OTYPE_PUSHPULL(GPIOB_ACCEL_CS) |   \
                                     PIN_OTYPE_OPENDRAIN(GPIOB_LED2) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOB_BOOT1) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOB_FLASH_SCK) |  \
                                     PIN_OTYPE_PUSHPULL(GPIOB_FLASH_MISO) | \
                                     PIN_OTYPE_PUSHPULL(GPIOB_FLASH_MOSI) | \
                                     PIN_OTYPE_OPENDRAIN(GPIOB_RTC_SDA) |   \
                                     PIN_OTYPE_OPENDRAIN(GPIOB_RTC_SCL) |   \
                                     PIN_OTYPE_PUSHPULL(GPIOB_PIN8) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOB_PIN9) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOB_PIN10) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOB_PIN11) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOB_PIN12) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOB_PIN13) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOB_PIN14) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOB_PIN15))
#define VAL_GPIOB_OSPEEDR           (PIN_OSPEED_LOW(GPIOB_ACCEL_CS) |       \
                                     PIN_OSPEED_LOW(GPIOB_LED2) |           \
                                     PIN_OSPEED_HIGH(GPIOB_BOOT1) |         \
                                     PIN_OSPEED_HIGH(GPIOB_FLASH_SCK) |     \
                                     PIN_OSPEED_HIGH(GPIOB_FLASH_MISO) |    \
                                     PIN_OSPEED_MEDIUM(GPIOB_FLASH_MOSI) |  \
                                     PIN_OSPEED_HIGH(GPIOB_RTC_SDA) |       \
                                     PIN_OSPEED_HIGH(GPIOB_RTC_SCL) |       \
                                     PIN_OSPEED_VERYLOW(GPIOB_PIN8) |       \
                                     PIN_OSPEED_VERYLOW(GPIOB_PIN9) |       \
                                     PIN_OSPEED_VERYLOW(GPIOB_PIN10) |      \
                                     PIN_OSPEED_VERYLOW(GPIOB_PIN11) |      \
                                     PIN_OSPEED_VERYLOW(GPIOB_PIN12) |      \
                                     PIN_OSPEED_VERYLOW(GPIOB_PIN13) |      \
                                     PIN_OSPEED_VERYLOW(GPIOB_PIN14) |      \
                                     PIN_OSPEED_VERYLOW(GPIOB_PIN15))
#define VAL_GPIOB_PUPDR             (PIN_PUPDR_FLOATING(GPIOB_ACCEL_CS) |   \
                                     PIN_PUPDR_FLOATING(GPIOB_LED2) |       \
                                     PIN_PUPDR_FLOATING(GPIOB_BOOT1) |      \
                                     PIN_PUPDR_FLOATING(GPIOB_FLASH_SCK) |  \
                                     PIN_PUPDR_FLOATING(GPIOB_FLASH_MISO) | \
                                     PIN_PUPDR_FLOATING(GPIOB_FLASH_MOSI) | \
                                     PIN_PUPDR_PULLUP(GPIOB_RTC_SDA) |      \
                                     PIN_PUPDR_PULLUP(GPIOB_RTC_SCL) |      \
                                     PIN_PUPDR_PULLUP(GPIOB_PIN8) |         \
                                     PIN_PUPDR_PULLUP(GPIOB_PIN9) |         \
                                     PIN_PUPDR_PULLUP(GPIOB_PIN10) |        \
                                     PIN_PUPDR_PULLUP(GPIOB_PIN11) |        \
                                     PIN_PUPDR_PULLUP(GPIOB_PIN12) |        \
                                     PIN_PUPDR_PULLUP(GPIOB_PIN13) |        \
                                     PIN_PUPDR_PULLUP(GPIOB_PIN14) |        \
                                     PIN_PUPDR_PULLUP(GPIOB_PIN15))
#define VAL_GPIOB_ODR               (PIN_ODR_HIGH(GPIOB_ACCEL_CS) |         \
                                     PIN_ODR_HIGH(GPIOB_LED2) |             \
                                     PIN_ODR_HIGH(GPIOB_BOOT1) |            \
                                     PIN_ODR_LOW(GPIOB_FLASH_SCK) |         \
                                     PIN_ODR_HIGH(GPIOB_FLASH_MISO) |       \
                                     PIN_ODR_LOW(GPIOB_FLASH_MOSI) |        \
                                     PIN_ODR_HIGH(GPIOB_RTC_SDA) |          \
                                     PIN_ODR_HIGH(GPIOB_RTC_SCL) |          \
                                     PIN_ODR_HIGH(GPIOB_PIN8) |             \
                                     PIN_ODR_HIGH(GPIOB_PIN9) |             \
                                     PIN_ODR_HIGH(GPIOB_PIN10) |            \
                                     PIN_ODR_HIGH(GPIOB_PIN11) |            \
                                     PIN_ODR_HIGH(GPIOB_PIN12) |            \
                                     PIN_ODR_HIGH(GPIOB_PIN13) |            \
                                     PIN_ODR_HIGH(GPIOB_PIN14) |            \
                                     PIN_ODR_HIGH(GPIOB_PIN15))
#define VAL_GPIOB_AFRL              (PIN_AFIO_AF(GPIOB_ACCEL_CS, 0U) |      \
                                     PIN_AFIO_AF(GPIOB_LED2, 0U) |          \
                                     PIN_AFIO_AF(GPIOB_BOOT1, 0U) |         \
                                     PIN_AFIO_AF(GPIOB_FLASH_SCK, 5U) |     \
                                     PIN_AFIO_AF(GPIOB_FLASH_MISO, 5U) |    \
                                     PIN_AFIO_AF(GPIOB_FLASH_MOSI, 5U) |    \
                                     PIN_AFIO_AF(GPIOB_RTC_SDA, 4U) |       \
                                     PIN_AFIO_AF(GPIOB_RTC_SCL, 4U))
#define VAL_GPIOB_AFRH              (PIN_AFIO_AF(GPIOB_PIN8, 0U) |          \
                                     PIN_AFIO_AF(GPIOB_PIN9, 0U) |          \
                                     PIN_AFIO_AF(GPIOB_PIN10, 0U) |         \
                                     PIN_AFIO_AF(GPIOB_PIN11, 0U) |         \
                                     PIN_AFIO_AF(GPIOB_PIN12, 0U) |         \
                                     PIN_AFIO_AF(GPIOB_PIN13, 0U) |         \
                                     PIN_AFIO_AF(GPIOB_PIN14, 0U) |         \
                                     PIN_AFIO_AF(GPIOB_PIN15, 0U))
#define VAL_GPIOB_ASCR              (PIN_ASCR_DISABLED(GPIOB_ACCEL_CS) |    \
                                     PIN_ASCR_DISABLED(GPIOB_LED2) |        \
                                     PIN_ASCR_DISABLED(GPIOB_BOOT1) |       \
                                     PIN_ASCR_DISABLED(GPIOB_FLASH_SCK) |   \
                                     PIN_ASCR_DISABLED(GPIOB_FLASH_MISO) |  \
                                     PIN_ASCR_DISABLED(GPIOB_FLASH_MOSI) |  \
                                     PIN_ASCR_DISABLED(GPIOB_RTC_SDA) |     \
                                     PIN_ASCR_DISABLED(GPIOB_RTC_SCL) |     \
                                     PIN_ASCR_DISABLED(GPIOB_PIN8) |        \
                                     PIN_ASCR_DISABLED(GPIOB_PIN9) |        \
                                     PIN_ASCR_DISABLED(GPIOB_PIN10) |       \
                                     PIN_ASCR_DISABLED(GPIOB_PIN11) |       \
                                     PIN_ASCR_DISABLED(GPIOB_PIN12) |       \
                                     PIN_ASCR_DISABLED(GPIOB_PIN13) |       \
                                     PIN_ASCR_DISABLED(GPIOB_PIN14) |       \
                                     PIN_ASCR_DISABLED(GPIOB_PIN15))
#define VAL_GPIOB_LOCKR             (PIN_LOCKR_DISABLED(GPIOB_ACCEL_CS) |   \
                                     PIN_LOCKR_DISABLED(GPIOB_LED2) |       \
                                     PIN_LOCKR_DISABLED(GPIOB_BOOT1) |      \
                                     PIN_LOCKR_DISABLED(GPIOB_FLASH_SCK) |  \
                                     PIN_LOCKR_DISABLED(GPIOB_FLASH_MISO) | \
                                     PIN_LOCKR_DISABLED(GPIOB_FLASH_MOSI) | \
                                     PIN_LOCKR_DISABLED(GPIOB_RTC_SDA) |    \
                                     PIN_LOCKR_DISABLED(GPIOB_RTC_SCL) |    \
                                     PIN_LOCKR_DISABLED(GPIOB_PIN8) |       \
                                     PIN_LOCKR_DISABLED(GPIOB_PIN9) |       \
                                     PIN_LOCKR_DISABLED(GPIOB_PIN10) |      \
                                     PIN_LOCKR_DISABLED(GPIOB_PIN11) |      \
                                     PIN_LOCKR_DISABLED(GPIOB_PIN12) |      \
                                     PIN_LOCKR_DISABLED(GPIOB_PIN13) |      \
                                     PIN_LOCKR_DISABLED(GPIOB_PIN14) |      \
                                     PIN_LOCKR_DISABLED(GPIOB_PIN15))

/*
 * GPIOC setup:
 *
 * PC0  - PIN0                      (analog).
 * PC1  - PIN1                      (analog).
 * PC2  - PIN2                      (analog).
 * PC3  - PIN3                      (analog).
 * PC4  - PIN4                      (analog).
 * PC5  - PIN5                      (analog).
 * PC6  - PIN6                      (analog).
 * PC7  - PIN7                      (analog).
 * PC8  - PIN8                      (analog).
 * PC9  - PIN9                      (analog).
 * PC10 - PIN10                     (analog).
 * PC11 - PIN11                     (analog).
 * PC12 - PIN12                     (analog).
 * PC13 - PIN13                     (analog).
 * PC14 - CLKIN                     (input floating).
 * PC15 - OSC32_OUT                 (analog).
 */
#define VAL_GPIOC_MODER             (PIN_MODE_ANALOG(GPIOC_PIN0) |          \
                                     PIN_MODE_ANALOG(GPIOC_PIN1) |          \
                                     PIN_MODE_ANALOG(GPIOC_PIN2) |          \
                                     PIN_MODE_ANALOG(GPIOC_PIN3) |          \
                                     PIN_MODE_ANALOG(GPIOC_PIN4) |          \
                                     PIN_MODE_ANALOG(GPIOC_PIN5) |          \
                                     PIN_MODE_ANALOG(GPIOC_PIN6) |          \
                                     PIN_MODE_ANALOG(GPIOC_PIN7) |          \
                                     PIN_MODE_ANALOG(GPIOC_PIN8) |          \
                                     PIN_MODE_ANALOG(GPIOC_PIN9) |          \
                                     PIN_MODE_ANALOG(GPIOC_PIN10) |         \
                                     PIN_MODE_ANALOG(GPIOC_PIN11) |         \
                                     PIN_MODE_ANALOG(GPIOC_PIN12) |         \
                                     PIN_MODE_ANALOG(GPIOC_PIN13) |         \
                                     PIN_MODE_INPUT(GPIOC_CLKIN) |          \
                                     PIN_MODE_ANALOG(GPIOC_OSC32_OUT))
#define VAL_GPIOC_OTYPER            (PIN_OTYPE_PUSHPULL(GPIOC_PIN0) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOC_PIN1) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOC_PIN2) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOC_PIN3) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOC_PIN4) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOC_PIN5) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOC_PIN6) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOC_PIN7) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOC_PIN8) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOC_PIN9) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOC_PIN10) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOC_PIN11) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOC_PIN12) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOC_PIN13) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOC_CLKIN) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOC_OSC32_OUT))
#define VAL_GPIOC_OSPEEDR           (PIN_OSPEED_VERYLOW(GPIOC_PIN0) |       \
                                     PIN_OSPEED_VERYLOW(GPIOC_PIN1) |       \
                                     PIN_OSPEED_VERYLOW(GPIOC_PIN2) |       \
                                     PIN_OSPEED_VERYLOW(GPIOC_PIN3) |       \
                                     PIN_OSPEED_VERYLOW(GPIOC_PIN4) |       \
                                     PIN_OSPEED_VERYLOW(GPIOC_PIN5) |       \
                                     PIN_OSPEED_VERYLOW(GPIOC_PIN6) |       \
                                     PIN_OSPEED_VERYLOW(GPIOC_PIN7) |       \
                                     PIN_OSPEED_VERYLOW(GPIOC_PIN8) |       \
                                     PIN_OSPEED_VERYLOW(GPIOC_PIN9) |       \
                                     PIN_OSPEED_VERYLOW(GPIOC_PIN10) |      \
                                     PIN_OSPEED_VERYLOW(GPIOC_PIN11) |      \
                                     PIN_OSPEED_VERYLOW(GPIOC_PIN12) |      \
                                     PIN_OSPEED_VERYLOW(GPIOC_PIN13) |      \
                                     PIN_OSPEED_HIGH(GPIOC_CLKIN) |         \
                                     PIN_OSPEED_HIGH(GPIOC_OSC32_OUT))
#define VAL_GPIOC_PUPDR             (PIN_PUPDR_PULLUP(GPIOC_PIN0) |         \
                                     PIN_PUPDR_PULLUP(GPIOC_PIN1) |         \
                                     PIN_PUPDR_PULLUP(GPIOC_PIN2) |         \
                                     PIN_PUPDR_PULLUP(GPIOC_PIN3) |         \
                                     PIN_PUPDR_PULLUP(GPIOC_PIN4) |         \
                                     PIN_PUPDR_PULLUP(GPIOC_PIN5) |         \
                                     PIN_PUPDR_PULLUP(GPIOC_PIN6) |         \
                                     PIN_PUPDR_PULLUP(GPIOC_PIN7) |         \
                                     PIN_PUPDR_PULLUP(GPIOC_PIN8) |         \
                                     PIN_PUPDR_PULLUP(GPIOC_PIN9) |         \
                                     PIN_PUPDR_PULLUP(GPIOC_PIN10) |        \
                                     PIN_PUPDR_PULLUP(GPIOC_PIN11) |        \
                                     PIN_PUPDR_PULLUP(GPIOC_PIN12) |        \
                                     PIN_PUPDR_PULLUP(GPIOC_PIN13) |        \
                                     PIN_PUPDR_FLOATING(GPIOC_CLKIN) |      \
                                     PIN_PUPDR_FLOATING(GPIOC_OSC32_OUT))
#define VAL_GPIOC_ODR               (PIN_ODR_HIGH(GPIOC_PIN0) |             \
                                     PIN_ODR_HIGH(GPIOC_PIN1) |             \
                                     PIN_ODR_HIGH(GPIOC_PIN2) |             \
                                     PIN_ODR_HIGH(GPIOC_PIN3) |             \
                                     PIN_ODR_HIGH(GPIOC_PIN4) |             \
                                     PIN_ODR_HIGH(GPIOC_PIN5) |             \
                                     PIN_ODR_HIGH(GPIOC_PIN6) |             \
                                     PIN_ODR_HIGH(GPIOC_PIN7) |             \
                                     PIN_ODR_HIGH(GPIOC_PIN8) |             \
                                     PIN_ODR_HIGH(GPIOC_PIN9) |             \
                                     PIN_ODR_HIGH(GPIOC_PIN10) |            \
                                     PIN_ODR_HIGH(GPIOC_PIN11) |            \
                                     PIN_ODR_HIGH(GPIOC_PIN12) |            \
                                     PIN_ODR_HIGH(GPIOC_PIN13) |            \
                                     PIN_ODR_HIGH(GPIOC_CLKIN) |            \
                                     PIN_ODR_HIGH(GPIOC_OSC32_OUT))
#define VAL_GPIOC_AFRL              (PIN_AFIO_AF(GPIOC_PIN0, 0U) |          \
                                     PIN_AFIO_AF(GPIOC_PIN1, 0U) |          \
                                     PIN_AFIO_AF(GPIOC_PIN2, 0U) |          \
                                     PIN_AFIO_AF(GPIOC_PIN3, 0U) |          \
                                     PIN_AFIO_AF(GPIOC_PIN4, 0U) |          \
                                     PIN_AFIO_AF(GPIOC_PIN5, 0U) |          \
                                     PIN_AFIO_AF(GPIOC_PIN6, 0U) |          \
                                     PIN_AFIO_AF(GPIOC_PIN7, 0U))
#define VAL_GPIOC_AFRH              (PIN_AFIO_AF(GPIOC_PIN8, 0U) |          \
                                     PIN_AFIO_AF(GPIOC_PIN9, 0U) |          \
                                     PIN_AFIO_AF(GPIOC_PIN10, 0U) |         \
                                     PIN_AFIO_AF(GPIOC_PIN11, 0U) |         \
                                     PIN_AFIO_AF(GPIOC_PIN12, 0U) |         \
                                     PIN_AFIO_AF(GPIOC_PIN13, 0U) |         \
                                     PIN_AFIO_AF(GPIOC_CLKIN, 0U) |         \
                                     PIN_AFIO_AF(GPIOC_OSC32_OUT, 0U))
#define VAL_GPIOC_ASCR              (PIN_ASCR_DISABLED(GPIOC_PIN0) |        \
                                     PIN_ASCR_DISABLED(GPIOC_PIN1) |        \
                                     PIN_ASCR_DISABLED(GPIOC_PIN2) |        \
                                     PIN_ASCR_DISABLED(GPIOC_PIN3) |        \
                                     PIN_ASCR_DISABLED(GPIOC_PIN4) |        \
                                     PIN_ASCR_DISABLED(GPIOC_PIN5) |        \
                                     PIN_ASCR_DISABLED(GPIOC_PIN6) |        \
                                     PIN_ASCR_DISABLED(GPIOC_PIN7) |        \
                                     PIN_ASCR_DISABLED(GPIOC_PIN8) |        \
                                     PIN_ASCR_DISABLED(GPIOC_PIN9) |        \
                                     PIN_ASCR_DISABLED(GPIOC_PIN10) |       \
                                     PIN_ASCR_DISABLED(GPIOC_PIN11) |       \
                                     PIN_ASCR_DISABLED(GPIOC_PIN12) |       \
                                     PIN_ASCR_DISABLED(GPIOC_PIN13) |       \
                                     PIN_ASCR_DISABLED(GPIOC_CLKIN) |       \
                                     PIN_ASCR_DISABLED(GPIOC_OSC32_OUT))
#define VAL_GPIOC_LOCKR             (PIN_LOCKR_DISABLED(GPIOC_PIN0) |       \
                                     PIN_LOCKR_DISABLED(GPIOC_PIN1) |       \
                                     PIN_LOCKR_DISABLED(GPIOC_PIN2) |       \
                                     PIN_LOCKR_DISABLED(GPIOC_PIN3) |       \
                                     PIN_LOCKR_DISABLED(GPIOC_PIN4) |       \
                                     PIN_LOCKR_DISABLED(GPIOC_PIN5) |       \
                                     PIN_LOCKR_DISABLED(GPIOC_PIN6) |       \
                                     PIN_LOCKR_DISABLED(GPIOC_PIN7) |       \
                                     PIN_LOCKR_DISABLED(GPIOC_PIN8) |       \
                                     PIN_LOCKR_DISABLED(GPIOC_PIN9) |       \
                                     PIN_LOCKR_DISABLED(GPIOC_PIN10) |      \
                                     PIN_LOCKR_DISABLED(GPIOC_PIN11) |      \
                                     PIN_LOCKR_DISABLED(GPIOC_PIN12) |      \
                                     PIN_LOCKR_DISABLED(GPIOC_PIN13) |      \
                                     PIN_LOCKR_DISABLED(GPIOC_CLKIN) |      \
                                     PIN_LOCKR_DISABLED(GPIOC_OSC32_OUT))

/*
 * GPIOD setup:
 *
 * PD0  - PIN0                      (analog).
 * PD1  - PIN1                      (analog).
 * PD2  - PIN2                      (analog).
 * PD3  - PIN3                      (analog).
 * PD4  - PIN4                      (analog).
 * PD5  - PIN5                      (analog).
 * PD6  - PIN6                      (analog).
 * PD7  - PIN7                      (analog).
 * PD8  - PIN8                      (analog).
 * PD9  - PIN9                      (analog).
 * PD10 - PIN10                     (analog).
 * PD11 - PIN11                     (analog).
 * PD12 - PIN12                     (analog).
 * PD13 - PIN13                     (analog).
 * PD14 - PIN14                     (analog).
 * PD15 - PIN15                     (analog).
 */
#define VAL_GPIOD_MODER             (PIN_MODE_ANALOG(GPIOD_PIN0) |          \
                                     PIN_MODE_ANALOG(GPIOD_PIN1) |          \
                                     PIN_MODE_ANALOG(GPIOD_PIN2) |          \
                                     PIN_MODE_ANALOG(GPIOD_PIN3) |          \
                                     PIN_MODE_ANALOG(GPIOD_PIN4) |          \
                                     PIN_MODE_ANALOG(GPIOD_PIN5) |          \
                                     PIN_MODE_ANALOG(GPIOD_PIN6) |          \
                                     PIN_MODE_ANALOG(GPIOD_PIN7) |          \
                                     PIN_MODE_ANALOG(GPIOD_PIN8) |          \
                                     PIN_MODE_ANALOG(GPIOD_PIN9) |          \
                                     PIN_MODE_ANALOG(GPIOD_PIN10) |         \
                                     PIN_MODE_ANALOG(GPIOD_PIN11) |         \
                                     PIN_MODE_ANALOG(GPIOD_PIN12) |         \
                                     PIN_MODE_ANALOG(GPIOD_PIN13) |         \
                                     PIN_MODE_ANALOG(GPIOD_PIN14) |         \
                                     PIN_MODE_ANALOG(GPIOD_PIN15))
#define VAL_GPIOD_OTYPER            (PIN_OTYPE_PUSHPULL(GPIOD_PIN0) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOD_PIN1) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOD_PIN2) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOD_PIN3) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOD_PIN4) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOD_PIN5) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOD_PIN6) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOD_PIN7) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOD_PIN8) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOD_PIN9) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOD_PIN10) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOD_PIN11) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOD_PIN12) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOD_PIN13) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOD_PIN14) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOD_PIN15))
#define VAL_GPIOD_OSPEEDR           (PIN_OSPEED_VERYLOW(GPIOD_PIN0) |       \
                                     PIN_OSPEED_VERYLOW(GPIOD_PIN1) |       \
                                     PIN_OSPEED_VERYLOW(GPIOD_PIN2) |       \
                                     PIN_OSPEED_VERYLOW(GPIOD_PIN3) |       \
                                     PIN_OSPEED_VERYLOW(GPIOD_PIN4) |       \
                                     PIN_OSPEED_VERYLOW(GPIOD_PIN5) |       \
                                     PIN_OSPEED_VERYLOW(GPIOD_PIN6) |       \
                                     PIN_OSPEED_VERYLOW(GPIOD_PIN7) |       \
                                     PIN_OSPEED_VERYLOW(GPIOD_PIN8) |       \
                                     PIN_OSPEED_VERYLOW(GPIOD_PIN9) |       \
                                     PIN_OSPEED_VERYLOW(GPIOD_PIN10) |      \
                                     PIN_OSPEED_VERYLOW(GPIOD_PIN11) |      \
                                     PIN_OSPEED_VERYLOW(GPIOD_PIN12) |      \
                                     PIN_OSPEED_VERYLOW(GPIOD_PIN13) |      \
                                     PIN_OSPEED_VERYLOW(GPIOD_PIN14) |      \
                                     PIN_OSPEED_VERYLOW(GPIOD_PIN15))
#define VAL_GPIOD_PUPDR             (PIN_PUPDR_PULLUP(GPIOD_PIN0) |         \
                                     PIN_PUPDR_PULLUP(GPIOD_PIN1) |         \
                                     PIN_PUPDR_PULLUP(GPIOD_PIN2) |         \
                                     PIN_PUPDR_PULLUP(GPIOD_PIN3) |         \
                                     PIN_PUPDR_PULLUP(GPIOD_PIN4) |         \
                                     PIN_PUPDR_PULLUP(GPIOD_PIN5) |         \
                                     PIN_PUPDR_PULLUP(GPIOD_PIN6) |         \
                                     PIN_PUPDR_PULLUP(GPIOD_PIN7) |         \
                                     PIN_PUPDR_PULLUP(GPIOD_PIN8) |         \
                                     PIN_PUPDR_PULLUP(GPIOD_PIN9) |         \
                                     PIN_PUPDR_PULLUP(GPIOD_PIN10) |        \
                                     PIN_PUPDR_PULLUP(GPIOD_PIN11) |        \
                                     PIN_PUPDR_PULLUP(GPIOD_PIN12) |        \
                                     PIN_PUPDR_PULLUP(GPIOD_PIN13) |        \
                                     PIN_PUPDR_PULLUP(GPIOD_PIN14) |        \
                                     PIN_PUPDR_PULLUP(GPIOD_PIN15))
#define VAL_GPIOD_ODR               (PIN_ODR_HIGH(GPIOD_PIN0) |             \
                                     PIN_ODR_HIGH(GPIOD_PIN1) |             \
                                     PIN_ODR_HIGH(GPIOD_PIN2) |             \
                                     PIN_ODR_HIGH(GPIOD_PIN3) |             \
                                     PIN_ODR_HIGH(GPIOD_PIN4) |             \
                                     PIN_ODR_HIGH(GPIOD_PIN5) |             \
                                     PIN_ODR_HIGH(GPIOD_PIN6) |             \
                                     PIN_ODR_HIGH(GPIOD_PIN7) |             \
                                     PIN_ODR_HIGH(GPIOD_PIN8) |             \
                                     PIN_ODR_HIGH(GPIOD_PIN9) |             \
                                     PIN_ODR_HIGH(GPIOD_PIN10) |            \
                                     PIN_ODR_HIGH(GPIOD_PIN11) |            \
                                     PIN_ODR_HIGH(GPIOD_PIN12) |            \
                                     PIN_ODR_HIGH(GPIOD_PIN13) |            \
                                     PIN_ODR_HIGH(GPIOD_PIN14) |            \
                                     PIN_ODR_HIGH(GPIOD_PIN15))
#define VAL_GPIOD_AFRL              (PIN_AFIO_AF(GPIOD_PIN0, 0U) |          \
                                     PIN_AFIO_AF(GPIOD_PIN1, 0U) |          \
                                     PIN_AFIO_AF(GPIOD_PIN2, 0U) |          \
                                     PIN_AFIO_AF(GPIOD_PIN3, 0U) |          \
                                     PIN_AFIO_AF(GPIOD_PIN4, 0U) |          \
                                     PIN_AFIO_AF(GPIOD_PIN5, 0U) |          \
                                     PIN_AFIO_AF(GPIOD_PIN6, 0U) |          \
                                     PIN_AFIO_AF(GPIOD_PIN7, 0U))
#define VAL_GPIOD_AFRH              (PIN_AFIO_AF(GPIOD_PIN8, 0U) |          \
                                     PIN_AFIO_AF(GPIOD_PIN9, 0U) |          \
                                     PIN_AFIO_AF(GPIOD_PIN10, 0U) |         \
                                     PIN_AFIO_AF(GPIOD_PIN11, 0U) |         \
                                     PIN_AFIO_AF(GPIOD_PIN12, 0U) |         \
                                     PIN_AFIO_AF(GPIOD_PIN13, 0U) |         \
                                     PIN_AFIO_AF(GPIOD_PIN14, 0U) |         \
                                     PIN_AFIO_AF(GPIOD_PIN15, 0U))
#define VAL_GPIOD_ASCR              (PIN_ASCR_DISABLED(GPIOD_PIN0) |        \
                                     PIN_ASCR_DISABLED(GPIOD_PIN1) |        \
                                     PIN_ASCR_DISABLED(GPIOD_PIN2) |        \
                                     PIN_ASCR_DISABLED(GPIOD_PIN3) |        \
                                     PIN_ASCR_DISABLED(GPIOD_PIN4) |        \
                                     PIN_ASCR_DISABLED(GPIOD_PIN5) |        \
                                     PIN_ASCR_DISABLED(GPIOD_PIN6) |        \
                                     PIN_ASCR_DISABLED(GPIOD_PIN7) |        \
                                     PIN_ASCR_DISABLED(GPIOD_PIN8) |        \
                                     PIN_ASCR_DISABLED(GPIOD_PIN9) |        \
                                     PIN_ASCR_DISABLED(GPIOD_PIN10) |       \
                                     PIN_ASCR_DISABLED(GPIOD_PIN11) |       \
                                     PIN_ASCR_DISABLED(GPIOD_PIN12) |       \
                                     PIN_ASCR_DISABLED(GPIOD_PIN13) |       \
                                     PIN_ASCR_DISABLED(GPIOD_PIN14) |       \
                                     PIN_ASCR_DISABLED(GPIOD_PIN15))
#define VAL_GPIOD_LOCKR             (PIN_LOCKR_DISABLED(GPIOD_PIN0) |       \
                                     PIN_LOCKR_DISABLED(GPIOD_PIN1) |       \
                                     PIN_LOCKR_DISABLED(GPIOD_PIN2) |       \
                                     PIN_LOCKR_DISABLED(GPIOD_PIN3) |       \
                                     PIN_LOCKR_DISABLED(GPIOD_PIN4) |       \
                                     PIN_LOCKR_DISABLED(GPIOD_PIN5) |       \
                                     PIN_LOCKR_DISABLED(GPIOD_PIN6) |       \
                                     PIN_LOCKR_DISABLED(GPIOD_PIN7) |       \
                                     PIN_LOCKR_DISABLED(GPIOD_PIN8) |       \
                                     PIN_LOCKR_DISABLED(GPIOD_PIN9) |       \
                                     PIN_LOCKR_DISABLED(GPIOD_PIN10) |      \
                                     PIN_LOCKR_DISABLED(GPIOD_PIN11) |      \
                                     PIN_LOCKR_DISABLED(GPIOD_PIN12) |      \
                                     PIN_LOCKR_DISABLED(GPIOD_PIN13) |      \
                                     PIN_LOCKR_DISABLED(GPIOD_PIN14) |      \
                                     PIN_LOCKR_DISABLED(GPIOD_PIN15))

/*
 * GPIOE setup:
 *
 * PE0  - PIN0                      (analog).
 * PE1  - PIN1                      (analog).
 * PE2  - PIN2                      (analog).
 * PE3  - PIN3                      (analog).
 * PE4  - PIN4                      (analog).
 * PE5  - PIN5                      (analog).
 * PE6  - PIN6                      (analog).
 * PE7  - PIN7                      (analog).
 * PE8  - PIN8                      (analog).
 * PE9  - PIN9                      (analog).
 * PE10 - PIN10                     (analog).
 * PE11 - PIN11                     (analog).
 * PE12 - PIN12                     (analog).
 * PE13 - PIN13                     (analog).
 * PE14 - PIN14                     (analog).
 * PE15 - PIN15                     (analog).
 */
#define VAL_GPIOE_MODER             (PIN_MODE_ANALOG(GPIOE_PIN0) |          \
                                     PIN_MODE_ANALOG(GPIOE_PIN1) |          \
                                     PIN_MODE_ANALOG(GPIOE_PIN2) |          \
                                     PIN_MODE_ANALOG(GPIOE_PIN3) |          \
                                     PIN_MODE_ANALOG(GPIOE_PIN4) |          \
                                     PIN_MODE_ANALOG(GPIOE_PIN5) |          \
                                     PIN_MODE_ANALOG(GPIOE_PIN6) |          \
                                     PIN_MODE_ANALOG(GPIOE_PIN7) |          \
                                     PIN_MODE_ANALOG(GPIOE_PIN8) |          \
                                     PIN_MODE_ANALOG(GPIOE_PIN9) |          \
                                     PIN_MODE_ANALOG(GPIOE_PIN10) |         \
                                     PIN_MODE_ANALOG(GPIOE_PIN11) |         \
                                     PIN_MODE_ANALOG(GPIOE_PIN12) |         \
                                     PIN_MODE_ANALOG(GPIOE_PIN13) |         \
                                     PIN_MODE_ANALOG(GPIOE_PIN14) |         \
                                     PIN_MODE_ANALOG(GPIOE_PIN15))
#define VAL_GPIOE_OTYPER            (PIN_OTYPE_PUSHPULL(GPIOE_PIN0) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOE_PIN1) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOE_PIN2) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOE_PIN3) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOE_PIN4) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOE_PIN5) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOE_PIN6) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOE_PIN7) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOE_PIN8) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOE_PIN9) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOE_PIN10) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOE_PIN11) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOE_PIN12) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOE_PIN13) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOE_PIN14) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOE_PIN15))
#define VAL_GPIOE_OSPEEDR           (PIN_OSPEED_VERYLOW(GPIOE_PIN0) |       \
                                     PIN_OSPEED_VERYLOW(GPIOE_PIN1) |       \
                                     PIN_OSPEED_VERYLOW(GPIOE_PIN2) |       \
                                     PIN_OSPEED_VERYLOW(GPIOE_PIN3) |       \
                                     PIN_OSPEED_VERYLOW(GPIOE_PIN4) |       \
                                     PIN_OSPEED_VERYLOW(GPIOE_PIN5) |       \
                                     PIN_OSPEED_VERYLOW(GPIOE_PIN6) |       \
                                     PIN_OSPEED_VERYLOW(GPIOE_PIN7) |       \
                                     PIN_OSPEED_VERYLOW(GPIOE_PIN8) |       \
                                     PIN_OSPEED_VERYLOW(GPIOE_PIN9) |       \
                                     PIN_OSPEED_VERYLOW(GPIOE_PIN10) |      \
                                     PIN_OSPEED_VERYLOW(GPIOE_PIN11) |      \
                                     PIN_OSPEED_VERYLOW(GPIOE_PIN12) |      \
                                     PIN_OSPEED_VERYLOW(GPIOE_PIN13) |      \
                                     PIN_OSPEED_VERYLOW(GPIOE_PIN14) |      \
                                     PIN_OSPEED_VERYLOW(GPIOE_PIN15))
#define VAL_GPIOE_PUPDR             (PIN_PUPDR_PULLUP(GPIOE_PIN0) |         \
                                     PIN_PUPDR_PULLUP(GPIOE_PIN1) |         \
                                     PIN_PUPDR_PULLUP(GPIOE_PIN2) |         \
                                     PIN_PUPDR_PULLUP(GPIOE_PIN3) |         \
                                     PIN_PUPDR_PULLUP(GPIOE_PIN4) |         \
                                     PIN_PUPDR_PULLUP(GPIOE_PIN5) |         \
                                     PIN_PUPDR_PULLUP(GPIOE_PIN6) |         \
                                     PIN_PUPDR_PULLUP(GPIOE_PIN7) |         \
                                     PIN_PUPDR_PULLUP(GPIOE_PIN8) |         \
                                     PIN_PUPDR_PULLUP(GPIOE_PIN9) |         \
                                     PIN_PUPDR_PULLUP(GPIOE_PIN10) |        \
                                     PIN_PUPDR_PULLUP(GPIOE_PIN11) |        \
                                     PIN_PUPDR_PULLUP(GPIOE_PIN12) |        \
                                     PIN_PUPDR_PULLUP(GPIOE_PIN13) |        \
                                     PIN_PUPDR_PULLUP(GPIOE_PIN14) |        \
                                     PIN_PUPDR_PULLUP(GPIOE_PIN15))
#define VAL_GPIOE_ODR               (PIN_ODR_HIGH(GPIOE_PIN0) |             \
                                     PIN_ODR_HIGH(GPIOE_PIN1) |             \
                                     PIN_ODR_HIGH(GPIOE_PIN2) |             \
                                     PIN_ODR_HIGH(GPIOE_PIN3) |             \
                                     PIN_ODR_HIGH(GPIOE_PIN4) |             \
                                     PIN_ODR_HIGH(GPIOE_PIN5) |             \
                                     PIN_ODR_HIGH(GPIOE_PIN6) |             \
                                     PIN_ODR_HIGH(GPIOE_PIN7) |             \
                                     PIN_ODR_HIGH(GPIOE_PIN8) |             \
                                     PIN_ODR_HIGH(GPIOE_PIN9) |             \
                                     PIN_ODR_HIGH(GPIOE_PIN10) |            \
                                     PIN_ODR_HIGH(GPIOE_PIN11) |            \
                                     PIN_ODR_HIGH(GPIOE_PIN12) |            \
                                     PIN_ODR_HIGH(GPIOE_PIN13) |            \
                                     PIN_ODR_HIGH(GPIOE_PIN14) |            \
                                     PIN_ODR_HIGH(GPIOE_PIN15))
#define VAL_GPIOE_AFRL              (PIN_AFIO_AF(GPIOE_PIN0, 0U) |          \
                                     PIN_AFIO_AF(GPIOE_PIN1, 0U) |          \
                                     PIN_AFIO_AF(GPIOE_PIN2, 0U) |          \
                                     PIN_AFIO_AF(GPIOE_PIN3, 0U) |          \
                                     PIN_AFIO_AF(GPIOE_PIN4, 0U) |          \
                                     PIN_AFIO_AF(GPIOE_PIN5, 0U) |          \
                                     PIN_AFIO_AF(GPIOE_PIN6, 0U) |          \
                                     PIN_AFIO_AF(GPIOE_PIN7, 0U))
#define VAL_GPIOE_AFRH              (PIN_AFIO_AF(GPIOE_PIN8, 0U) |          \
                                     PIN_AFIO_AF(GPIOE_PIN9, 0U) |          \
                                     PIN_AFIO_AF(GPIOE_PIN10, 0U) |         \
                                     PIN_AFIO_AF(GPIOE_PIN11, 0U) |         \
                                     PIN_AFIO_AF(GPIOE_PIN12, 0U) |         \
                                     PIN_AFIO_AF(GPIOE_PIN13, 0U) |         \
                                     PIN_AFIO_AF(GPIOE_PIN14, 0U) |         \
                                     PIN_AFIO_AF(GPIOE_PIN15, 0U))
#define VAL_GPIOE_ASCR              (PIN_ASCR_DISABLED(GPIOE_PIN0) |        \
                                     PIN_ASCR_DISABLED(GPIOE_PIN1) |        \
                                     PIN_ASCR_DISABLED(GPIOE_PIN2) |        \
                                     PIN_ASCR_DISABLED(GPIOE_PIN3) |        \
                                     PIN_ASCR_DISABLED(GPIOE_PIN4) |        \
                                     PIN_ASCR_DISABLED(GPIOE_PIN5) |        \
                                     PIN_ASCR_DISABLED(GPIOE_PIN6) |        \
                                     PIN_ASCR_DISABLED(GPIOE_PIN7) |        \
                                     PIN_ASCR_DISABLED(GPIOE_PIN8) |        \
                                     PIN_ASCR_DISABLED(GPIOE_PIN9) |        \
                                     PIN_ASCR_DISABLED(GPIOE_PIN10) |       \
                                     PIN_ASCR_DISABLED(GPIOE_PIN11) |       \
                                     PIN_ASCR_DISABLED(GPIOE_PIN12) |       \
                                     PIN_ASCR_DISABLED(GPIOE_PIN13) |       \
                                     PIN_ASCR_DISABLED(GPIOE_PIN14) |       \
                                     PIN_ASCR_DISABLED(GPIOE_PIN15))
#define VAL_GPIOE_LOCKR             (PIN_LOCKR_DISABLED(GPIOE_PIN0) |       \
                                     PIN_LOCKR_DISABLED(GPIOE_PIN1) |       \
                                     PIN_LOCKR_DISABLED(GPIOE_PIN2) |       \
                                     PIN_LOCKR_DISABLED(GPIOE_PIN3) |       \
                                     PIN_LOCKR_DISABLED(GPIOE_PIN4) |       \
                                     PIN_LOCKR_DISABLED(GPIOE_PIN5) |       \
                                     PIN_LOCKR_DISABLED(GPIOE_PIN6) |       \
                                     PIN_LOCKR_DISABLED(GPIOE_PIN7) |       \
                                     PIN_LOCKR_DISABLED(GPIOE_PIN8) |       \
                                     PIN_LOCKR_DISABLED(GPIOE_PIN9) |       \
                                     PIN_LOCKR_DISABLED(GPIOE_PIN10) |      \
                                     PIN_LOCKR_DISABLED(GPIOE_PIN11) |      \
                                     PIN_LOCKR_DISABLED(GPIOE_PIN12) |      \
                                     PIN_LOCKR_DISABLED(GPIOE_PIN13) |      \
                                     PIN_LOCKR_DISABLED(GPIOE_PIN14) |      \
                                     PIN_LOCKR_DISABLED(GPIOE_PIN15))

/*
 * GPIOF setup:
 *
 * PF0  - PIN0                      (analog).
 * PF1  - PIN1                      (analog).
 * PF2  - PIN2                      (analog).
 * PF3  - PIN3                      (analog).
 * PF4  - PIN4                      (analog).
 * PF5  - PIN5                      (analog).
 * PF6  - PIN6                      (analog).
 * PF7  - PIN7                      (analog).
 * PF8  - PIN8                      (analog).
 * PF9  - PIN9                      (analog).
 * PF10 - PIN10                     (analog).
 * PF11 - PIN11                     (analog).
 * PF12 - PIN12                     (analog).
 * PF13 - PIN13                     (analog).
 * PF14 - PIN14                     (analog).
 * PF15 - PIN15                     (analog).
 */
#define VAL_GPIOF_MODER             (PIN_MODE_ANALOG(GPIOF_PIN0) |          \
                                     PIN_MODE_ANALOG(GPIOF_PIN1) |          \
                                     PIN_MODE_ANALOG(GPIOF_PIN2) |          \
                                     PIN_MODE_ANALOG(GPIOF_PIN3) |          \
                                     PIN_MODE_ANALOG(GPIOF_PIN4) |          \
                                     PIN_MODE_ANALOG(GPIOF_PIN5) |          \
                                     PIN_MODE_ANALOG(GPIOF_PIN6) |          \
                                     PIN_MODE_ANALOG(GPIOF_PIN7) |          \
                                     PIN_MODE_ANALOG(GPIOF_PIN8) |          \
                                     PIN_MODE_ANALOG(GPIOF_PIN9) |          \
                                     PIN_MODE_ANALOG(GPIOF_PIN10) |         \
                                     PIN_MODE_ANALOG(GPIOF_PIN11) |         \
                                     PIN_MODE_ANALOG(GPIOF_PIN12) |         \
                                     PIN_MODE_ANALOG(GPIOF_PIN13) |         \
                                     PIN_MODE_ANALOG(GPIOF_PIN14) |         \
                                     PIN_MODE_ANALOG(GPIOF_PIN15))
#define VAL_GPIOF_OTYPER            (PIN_OTYPE_PUSHPULL(GPIOF_PIN0) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOF_PIN1) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOF_PIN2) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOF_PIN3) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOF_PIN4) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOF_PIN5) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOF_PIN6) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOF_PIN7) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOF_PIN8) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOF_PIN9) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOF_PIN10) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOF_PIN11) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOF_PIN12) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOF_PIN13) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOF_PIN14) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOF_PIN15))
#define VAL_GPIOF_OSPEEDR           (PIN_OSPEED_VERYLOW(GPIOF_PIN0) |       \
                                     PIN_OSPEED_VERYLOW(GPIOF_PIN1) |       \
                                     PIN_OSPEED_VERYLOW(GPIOF_PIN2) |       \
                                     PIN_OSPEED_VERYLOW(GPIOF_PIN3) |       \
                                     PIN_OSPEED_VERYLOW(GPIOF_PIN4) |       \
                                     PIN_OSPEED_VERYLOW(GPIOF_PIN5) |       \
                                     PIN_OSPEED_VERYLOW(GPIOF_PIN6) |       \
                                     PIN_OSPEED_VERYLOW(GPIOF_PIN7) |       \
                                     PIN_OSPEED_VERYLOW(GPIOF_PIN8) |       \
                                     PIN_OSPEED_VERYLOW(GPIOF_PIN9) |       \
                                     PIN_OSPEED_VERYLOW(GPIOF_PIN10) |      \
                                     PIN_OSPEED_VERYLOW(GPIOF_PIN11) |      \
                                     PIN_OSPEED_VERYLOW(GPIOF_PIN12) |      \
                                     PIN_OSPEED_VERYLOW(GPIOF_PIN13) |      \
                                     PIN_OSPEED_VERYLOW(GPIOF_PIN14) |      \
                                     PIN_OSPEED_VERYLOW(GPIOF_PIN15))
#define VAL_GPIOF_PUPDR             (PIN_PUPDR_PULLUP(GPIOF_PIN0) |         \
                                     PIN_PUPDR_PULLUP(GPIOF_PIN1) |         \
                                     PIN_PUPDR_PULLUP(GPIOF_PIN2) |         \
                                     PIN_PUPDR_PULLUP(GPIOF_PIN3) |         \
                                     PIN_PUPDR_PULLUP(GPIOF_PIN4) |         \
                                     PIN_PUPDR_PULLUP(GPIOF_PIN5) |         \
                                     PIN_PUPDR_PULLUP(GPIOF_PIN6) |         \
                                     PIN_PUPDR_PULLUP(GPIOF_PIN7) |         \
                                     PIN_PUPDR_PULLUP(GPIOF_PIN8) |         \
                                     PIN_PUPDR_PULLUP(GPIOF_PIN9) |         \
                                     PIN_PUPDR_PULLUP(GPIOF_PIN10) |        \
                                     PIN_PUPDR_PULLUP(GPIOF_PIN11) |        \
                                     PIN_PUPDR_PULLUP(GPIOF_PIN12) |        \
                                     PIN_PUPDR_PULLUP(GPIOF_PIN13) |        \
                                     PIN_PUPDR_PULLUP(GPIOF_PIN14) |        \
                                     PIN_PUPDR_PULLUP(GPIOF_PIN15))
#define VAL_GPIOF_ODR               (PIN_ODR_HIGH(GPIOF_PIN0) |             \
                                     PIN_ODR_HIGH(GPIOF_PIN1) |             \
                                     PIN_ODR_HIGH(GPIOF_PIN2) |             \
                                     PIN_ODR_HIGH(GPIOF_PIN3) |             \
                                     PIN_ODR_HIGH(GPIOF_PIN4) |             \
                                     PIN_ODR_HIGH(GPIOF_PIN5) |             \
                                     PIN_ODR_HIGH(GPIOF_PIN6) |             \
                                     PIN_ODR_HIGH(GPIOF_PIN7) |             \
                                     PIN_ODR_HIGH(GPIOF_PIN8) |             \
                                     PIN_ODR_HIGH(GPIOF_PIN9) |             \
                                     PIN_ODR_HIGH(GPIOF_PIN10) |            \
                                     PIN_ODR_HIGH(GPIOF_PIN11) |            \
                                     PIN_ODR_HIGH(GPIOF_PIN12) |            \
                                     PIN_ODR_HIGH(GPIOF_PIN13) |            \
                                     PIN_ODR_HIGH(GPIOF_PIN14) |            \
                                     PIN_ODR_HIGH(GPIOF_PIN15))
#define VAL_GPIOF_AFRL              (PIN_AFIO_AF(GPIOF_PIN0, 0U) |          \
                                     PIN_AFIO_AF(GPIOF_PIN1, 0U) |          \
                                     PIN_AFIO_AF(GPIOF_PIN2, 0U) |          \
                                     PIN_AFIO_AF(GPIOF_PIN3, 0U) |          \
                                     PIN_AFIO_AF(GPIOF_PIN4, 0U) |          \
                                     PIN_AFIO_AF(GPIOF_PIN5, 0U) |          \
                                     PIN_AFIO_AF(GPIOF_PIN6, 0U) |          \
                                     PIN_AFIO_AF(GPIOF_PIN7, 0U))
#define VAL_GPIOF_AFRH              (PIN_AFIO_AF(GPIOF_PIN8, 0U) |          \
                                     PIN_AFIO_AF(GPIOF_PIN9, 0U) |          \
                                     PIN_AFIO_AF(GPIOF_PIN10, 0U) |         \
                                     PIN_AFIO_AF(GPIOF_PIN11, 0U) |         \
                                     PIN_AFIO_AF(GPIOF_PIN12, 0U) |         \
                                     PIN_AFIO_AF(GPIOF_PIN13, 0U) |         \
                                     PIN_AFIO_AF(GPIOF_PIN14, 0U) |         \
                                     PIN_AFIO_AF(GPIOF_PIN15, 0U))
#define VAL_GPIOF_ASCR              (PIN_ASCR_DISABLED(GPIOF_PIN0) |        \
                                     PIN_ASCR_DISABLED(GPIOF_PIN1) |        \
                                     PIN_ASCR_DISABLED(GPIOF_PIN2) |        \
                                     PIN_ASCR_DISABLED(GPIOF_PIN3) |        \
                                     PIN_ASCR_DISABLED(GPIOF_PIN4) |        \
                                     PIN_ASCR_DISABLED(GPIOF_PIN5) |        \
                                     PIN_ASCR_DISABLED(GPIOF_PIN6) |        \
                                     PIN_ASCR_DISABLED(GPIOF_PIN7) |        \
                                     PIN_ASCR_DISABLED(GPIOF_PIN8) |        \
                                     PIN_ASCR_DISABLED(GPIOF_PIN9) |        \
                                     PIN_ASCR_DISABLED(GPIOF_PIN10) |       \
                                     PIN_ASCR_DISABLED(GPIOF_PIN11) |       \
                                     PIN_ASCR_DISABLED(GPIOF_PIN12) |       \
                                     PIN_ASCR_DISABLED(GPIOF_PIN13) |       \
                                     PIN_ASCR_DISABLED(GPIOF_PIN14) |       \
                                     PIN_ASCR_DISABLED(GPIOF_PIN15))
#define VAL_GPIOF_LOCKR             (PIN_LOCKR_DISABLED(GPIOF_PIN0) |       \
                                     PIN_LOCKR_DISABLED(GPIOF_PIN1) |       \
                                     PIN_LOCKR_DISABLED(GPIOF_PIN2) |       \
                                     PIN_LOCKR_DISABLED(GPIOF_PIN3) |       \
                                     PIN_LOCKR_DISABLED(GPIOF_PIN4) |       \
                                     PIN_LOCKR_DISABLED(GPIOF_PIN5) |       \
                                     PIN_LOCKR_DISABLED(GPIOF_PIN6) |       \
                                     PIN_LOCKR_DISABLED(GPIOF_PIN7) |       \
                                     PIN_LOCKR_DISABLED(GPIOF_PIN8) |       \
                                     PIN_LOCKR_DISABLED(GPIOF_PIN9) |       \
                                     PIN_LOCKR_DISABLED(GPIOF_PIN10) |      \
                                     PIN_LOCKR_DISABLED(GPIOF_PIN11) |      \
                                     PIN_LOCKR_DISABLED(GPIOF_PIN12) |      \
                                     PIN_LOCKR_DISABLED(GPIOF_PIN13) |      \
                                     PIN_LOCKR_DISABLED(GPIOF_PIN14) |      \
                                     PIN_LOCKR_DISABLED(GPIOF_PIN15))

/*
 * GPIOG setup:
 *
 * PG0  - PIN0                      (analog).
 * PG1  - PIN1                      (analog).
 * PG2  - PIN2                      (analog).
 * PG3  - PIN3                      (analog).
 * PG4  - PIN4                      (analog).
 * PG5  - PIN5                      (analog).
 * PG6  - PIN6                      (analog).
 * PG7  - PIN7                      (analog).
 * PG8  - PIN8                      (analog).
 * PG9  - PIN9                      (analog).
 * PG10 - PIN10                     (analog).
 * PG11 - PIN11                     (analog).
 * PG12 - PIN12                     (analog).
 * PG13 - PIN13                     (analog).
 * PG14 - PIN14                     (analog).
 * PG15 - PIN15                     (analog).
 */
#define VAL_GPIOG_MODER             (PIN_MODE_ANALOG(GPIOG_PIN0) |          \
                                     PIN_MODE_ANALOG(GPIOG_PIN1) |          \
                                     PIN_MODE_ANALOG(GPIOG_PIN2) |          \
                                     PIN_MODE_ANALOG(GPIOG_PIN3) |          \
                                     PIN_MODE_ANALOG(GPIOG_PIN4) |          \
                                     PIN_MODE_ANALOG(GPIOG_PIN5) |          \
                                     PIN_MODE_ANALOG(GPIOG_PIN6) |          \
                                     PIN_MODE_ANALOG(GPIOG_PIN7) |          \
                                     PIN_MODE_ANALOG(GPIOG_PIN8) |          \
                                     PIN_MODE_ANALOG(GPIOG_PIN9) |          \
                                     PIN_MODE_ANALOG(GPIOG_PIN10) |         \
                                     PIN_MODE_ANALOG(GPIOG_PIN11) |         \
                                     PIN_MODE_ANALOG(GPIOG_PIN12) |         \
                                     PIN_MODE_ANALOG(GPIOG_PIN13) |         \
                                     PIN_MODE_ANALOG(GPIOG_PIN14) |         \
                                     PIN_MODE_ANALOG(GPIOG_PIN15))
#define VAL_GPIOG_OTYPER            (PIN_OTYPE_PUSHPULL(GPIOG_PIN0) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOG_PIN1) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOG_PIN2) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOG_PIN3) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOG_PIN4) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOG_PIN5) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOG_PIN6) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOG_PIN7) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOG_PIN8) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOG_PIN9) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOG_PIN10) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOG_PIN11) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOG_PIN12) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOG_PIN13) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOG_PIN14) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOG_PIN15))
#define VAL_GPIOG_OSPEEDR           (PIN_OSPEED_VERYLOW(GPIOG_PIN0) |       \
                                     PIN_OSPEED_VERYLOW(GPIOG_PIN1) |       \
                                     PIN_OSPEED_VERYLOW(GPIOG_PIN2) |       \
                                     PIN_OSPEED_VERYLOW(GPIOG_PIN3) |       \
                                     PIN_OSPEED_VERYLOW(GPIOG_PIN4) |       \
                                     PIN_OSPEED_VERYLOW(GPIOG_PIN5) |       \
                                     PIN_OSPEED_VERYLOW(GPIOG_PIN6) |       \
                                     PIN_OSPEED_VERYLOW(GPIOG_PIN7) |       \
                                     PIN_OSPEED_VERYLOW(GPIOG_PIN8) |       \
                                     PIN_OSPEED_VERYLOW(GPIOG_PIN9) |       \
                                     PIN_OSPEED_VERYLOW(GPIOG_PIN10) |      \
                                     PIN_OSPEED_VERYLOW(GPIOG_PIN11) |      \
                                     PIN_OSPEED_VERYLOW(GPIOG_PIN12) |      \
                                     PIN_OSPEED_VERYLOW(GPIOG_PIN13) |      \
                                     PIN_OSPEED_VERYLOW(GPIOG_PIN14) |      \
                                     PIN_OSPEED_VERYLOW(GPIOG_PIN15))
#define VAL_GPIOG_PUPDR             (PIN_PUPDR_PULLUP(GPIOG_PIN0) |         \
                                     PIN_PUPDR_PULLUP(GPIOG_PIN1) |         \
                                     PIN_PUPDR_PULLUP(GPIOG_PIN2) |         \
                                     PIN_PUPDR_PULLUP(GPIOG_PIN3) |         \
                                     PIN_PUPDR_PULLUP(GPIOG_PIN4) |         \
                                     PIN_PUPDR_PULLUP(GPIOG_PIN5) |         \
                                     PIN_PUPDR_PULLUP(GPIOG_PIN6) |         \
                                     PIN_PUPDR_PULLUP(GPIOG_PIN7) |         \
                                     PIN_PUPDR_PULLUP(GPIOG_PIN8) |         \
                                     PIN_PUPDR_PULLUP(GPIOG_PIN9) |         \
                                     PIN_PUPDR_PULLUP(GPIOG_PIN10) |        \
                                     PIN_PUPDR_PULLUP(GPIOG_PIN11) |        \
                                     PIN_PUPDR_PULLUP(GPIOG_PIN12) |        \
                                     PIN_PUPDR_PULLUP(GPIOG_PIN13) |        \
                                     PIN_PUPDR_PULLUP(GPIOG_PIN14) |        \
                                     PIN_PUPDR_PULLUP(GPIOG_PIN15))
#define VAL_GPIOG_ODR               (PIN_ODR_HIGH(GPIOG_PIN0) |             \
                                     PIN_ODR_HIGH(GPIOG_PIN1) |             \
                                     PIN_ODR_HIGH(GPIOG_PIN2) |             \
                                     PIN_ODR_HIGH(GPIOG_PIN3) |             \
                                     PIN_ODR_HIGH(GPIOG_PIN4) |             \
                                     PIN_ODR_HIGH(GPIOG_PIN5) |             \
                                     PIN_ODR_HIGH(GPIOG_PIN6) |             \
                                     PIN_ODR_HIGH(GPIOG_PIN7) |             \
                                     PIN_ODR_HIGH(GPIOG_PIN8) |             \
                                     PIN_ODR_HIGH(GPIOG_PIN9) |             \
                                     PIN_ODR_HIGH(GPIOG_PIN10) |            \
                                     PIN_ODR_HIGH(GPIOG_PIN11) |            \
                                     PIN_ODR_HIGH(GPIOG_PIN12) |            \
                                     PIN_ODR_HIGH(GPIOG_PIN13) |            \
                                     PIN_ODR_HIGH(GPIOG_PIN14) |            \
                                     PIN_ODR_HIGH(GPIOG_PIN15))
#define VAL_GPIOG_AFRL              (PIN_AFIO_AF(GPIOG_PIN0, 0U) |          \
                                     PIN_AFIO_AF(GPIOG_PIN1, 0U) |          \
                                     PIN_AFIO_AF(GPIOG_PIN2, 0U) |          \
                                     PIN_AFIO_AF(GPIOG_PIN3, 0U) |          \
                                     PIN_AFIO_AF(GPIOG_PIN4, 0U) |          \
                                     PIN_AFIO_AF(GPIOG_PIN5, 0U) |          \
                                     PIN_AFIO_AF(GPIOG_PIN6, 0U) |          \
                                     PIN_AFIO_AF(GPIOG_PIN7, 0U))
#define VAL_GPIOG_AFRH              (PIN_AFIO_AF(GPIOG_PIN8, 0U) |          \
                                     PIN_AFIO_AF(GPIOG_PIN9, 0U) |          \
                                     PIN_AFIO_AF(GPIOG_PIN10, 0U) |         \
                                     PIN_AFIO_AF(GPIOG_PIN11, 0U) |         \
                                     PIN_AFIO_AF(GPIOG_PIN12, 0U) |         \
                                     PIN_AFIO_AF(GPIOG_PIN13, 0U) |         \
                                     PIN_AFIO_AF(GPIOG_PIN14, 0U) |         \
                                     PIN_AFIO_AF(GPIOG_PIN15, 0U))
#define VAL_GPIOG_ASCR              (PIN_ASCR_DISABLED(GPIOG_PIN0) |        \
                                     PIN_ASCR_DISABLED(GPIOG_PIN1) |        \
                                     PIN_ASCR_DISABLED(GPIOG_PIN2) |        \
                                     PIN_ASCR_DISABLED(GPIOG_PIN3) |        \
                                     PIN_ASCR_DISABLED(GPIOG_PIN4) |        \
                                     PIN_ASCR_DISABLED(GPIOG_PIN5) |        \
                                     PIN_ASCR_DISABLED(GPIOG_PIN6) |        \
                                     PIN_ASCR_DISABLED(GPIOG_PIN7) |        \
                                     PIN_ASCR_DISABLED(GPIOG_PIN8) |        \
                                     PIN_ASCR_DISABLED(GPIOG_PIN9) |        \
                                     PIN_ASCR_DISABLED(GPIOG_PIN10) |       \
                                     PIN_ASCR_DISABLED(GPIOG_PIN11) |       \
                                     PIN_ASCR_DISABLED(GPIOG_PIN12) |       \
                                     PIN_ASCR_DISABLED(GPIOG_PIN13) |       \
                                     PIN_ASCR_DISABLED(GPIOG_PIN14) |       \
                                     PIN_ASCR_DISABLED(GPIOG_PIN15))
#define VAL_GPIOG_LOCKR             (PIN_LOCKR_DISABLED(GPIOG_PIN0) |       \
                                     PIN_LOCKR_DISABLED(GPIOG_PIN1) |       \
                                     PIN_LOCKR_DISABLED(GPIOG_PIN2) |       \
                                     PIN_LOCKR_DISABLED(GPIOG_PIN3) |       \
                                     PIN_LOCKR_DISABLED(GPIOG_PIN4) |       \
                                     PIN_LOCKR_DISABLED(GPIOG_PIN5) |       \
                                     PIN_LOCKR_DISABLED(GPIOG_PIN6) |       \
                                     PIN_LOCKR_DISABLED(GPIOG_PIN7) |       \
                                     PIN_LOCKR_DISABLED(GPIOG_PIN8) |       \
                                     PIN_LOCKR_DISABLED(GPIOG_PIN9) |       \
                                     PIN_LOCKR_DISABLED(GPIOG_PIN10) |      \
                                     PIN_LOCKR_DISABLED(GPIOG_PIN11) |      \
                                     PIN_LOCKR_DISABLED(GPIOG_PIN12) |      \
                                     PIN_LOCKR_DISABLED(GPIOG_PIN13) |      \
                                     PIN_LOCKR_DISABLED(GPIOG_PIN14) |      \
                                     PIN_LOCKR_DISABLED(GPIOG_PIN15))

/*
 * GPIOH setup:
 *
 * PH0  - OSC_IN                    (analog).
 * PH1  - OSC_OUT                   (analog).
 * PH2  - PIN2                      (analog).
 * PH3  - PIN3                      (analog).
 * PH4  - PIN4                      (analog).
 * PH5  - PIN5                      (analog).
 * PH6  - PIN6                      (analog).
 * PH7  - PIN7                      (analog).
 * PH8  - PIN8                      (analog).
 * PH9  - PIN9                      (analog).
 * PH10 - PIN10                     (analog).
 * PH11 - PIN11                     (analog).
 * PH12 - PIN12                     (analog).
 * PH13 - PIN13                     (analog).
 * PH14 - PIN14                     (analog).
 * PH15 - PIN15                     (analog).
 */
#define VAL_GPIOH_MODER             (PIN_MODE_ANALOG(GPIOH_OSC_IN) |        \
                                     PIN_MODE_ANALOG(GPIOH_OSC_OUT) |       \
                                     PIN_MODE_ANALOG(GPIOH_PIN2) |          \
                                     PIN_MODE_ANALOG(GPIOH_PIN3) |          \
                                     PIN_MODE_ANALOG(GPIOH_PIN4) |          \
                                     PIN_MODE_ANALOG(GPIOH_PIN5) |          \
                                     PIN_MODE_ANALOG(GPIOH_PIN6) |          \
                                     PIN_MODE_ANALOG(GPIOH_PIN7) |          \
                                     PIN_MODE_ANALOG(GPIOH_PIN8) |          \
                                     PIN_MODE_ANALOG(GPIOH_PIN9) |          \
                                     PIN_MODE_ANALOG(GPIOH_PIN10) |         \
                                     PIN_MODE_ANALOG(GPIOH_PIN11) |         \
                                     PIN_MODE_ANALOG(GPIOH_PIN12) |         \
                                     PIN_MODE_ANALOG(GPIOH_PIN13) |         \
                                     PIN_MODE_ANALOG(GPIOH_PIN14) |         \
                                     PIN_MODE_ANALOG(GPIOH_PIN15))
#define VAL_GPIOH_OTYPER            (PIN_OTYPE_PUSHPULL(GPIOH_OSC_IN) |     \
                                     PIN_OTYPE_PUSHPULL(GPIOH_OSC_OUT) |    \
                                     PIN_OTYPE_PUSHPULL(GPIOH_PIN2) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOH_PIN3) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOH_PIN4) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOH_PIN5) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOH_PIN6) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOH_PIN7) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOH_PIN8) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOH_PIN9) |       \
                                     PIN_OTYPE_PUSHPULL(GPIOH_PIN10) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOH_PIN11) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOH_PIN12) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOH_PIN13) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOH_PIN14) |      \
                                     PIN_OTYPE_PUSHPULL(GPIOH_PIN15))
#define VAL_GPIOH_OSPEEDR           (PIN_OSPEED_HIGH(GPIOH_OSC_IN) |        \
                                     PIN_OSPEED_HIGH(GPIOH_OSC_OUT) |       \
                                     PIN_OSPEED_VERYLOW(GPIOH_PIN2) |       \
                                     PIN_OSPEED_VERYLOW(GPIOH_PIN3) |       \
                                     PIN_OSPEED_VERYLOW(GPIOH_PIN4) |       \
                                     PIN_OSPEED_VERYLOW(GPIOH_PIN5) |       \
                                     PIN_OSPEED_VERYLOW(GPIOH_PIN6) |       \
                                     PIN_OSPEED_VERYLOW(GPIOH_PIN7) |       \
                                     PIN_OSPEED_VERYLOW(GPIOH_PIN8) |       \
                                     PIN_OSPEED_VERYLOW(GPIOH_PIN9) |       \
                                     PIN_OSPEED_VERYLOW(GPIOH_PIN10) |      \
                                     PIN_OSPEED_VERYLOW(GPIOH_PIN11) |      \
                                     PIN_OSPEED_VERYLOW(GPIOH_PIN12) |      \
                                     PIN_OSPEED_VERYLOW(GPIOH_PIN13) |      \
                                     PIN_OSPEED_VERYLOW(GPIOH_PIN14) |      \
                                     PIN_OSPEED_VERYLOW(GPIOH_PIN15))
#define VAL_GPIOH_PUPDR             (PIN_PUPDR_FLOATING(GPIOH_OSC_IN) |     \
                                     PIN_PUPDR_FLOATING(GPIOH_OSC_OUT) |    \
                                     PIN_PUPDR_PULLUP(GPIOH_PIN2) |         \
                                     PIN_PUPDR_PULLUP(GPIOH_PIN3) |         \
                                     PIN_PUPDR_PULLUP(GPIOH_PIN4) |         \
                                     PIN_PUPDR_PULLUP(GPIOH_PIN5) |         \
                                     PIN_PUPDR_PULLUP(GPIOH_PIN6) |         \
                                     PIN_PUPDR_PULLUP(GPIOH_PIN7) |         \
                                     PIN_PUPDR_PULLUP(GPIOH_PIN8) |         \
                                     PIN_PUPDR_PULLUP(GPIOH_PIN9) |         \
                                     PIN_PUPDR_PULLUP(GPIOH_PIN10) |        \
                                     PIN_PUPDR_PULLUP(GPIOH_PIN11) |        \
                                     PIN_PUPDR_PULLUP(GPIOH_PIN12) |        \
                                     PIN_PUPDR_PULLUP(GPIOH_PIN13) |        \
                                     PIN_PUPDR_PULLUP(GPIOH_PIN14) |        \
                                     PIN_PUPDR_PULLUP(GPIOH_PIN15))
#define VAL_GPIOH_ODR               (PIN_ODR_HIGH(GPIOH_OSC_IN) |           \
                                     PIN_ODR_HIGH(GPIOH_OSC_OUT) |          \
                                     PIN_ODR_HIGH(GPIOH_PIN2) |             \
                                     PIN_ODR_HIGH(GPIOH_PIN3) |             \
                                     PIN_ODR_HIGH(GPIOH_PIN4) |             \
                                     PIN_ODR_HIGH(GPIOH_PIN5) |             \
                                     PIN_ODR_HIGH(GPIOH_PIN6) |             \
                                     PIN_ODR_HIGH(GPIOH_PIN7) |             \
                                     PIN_ODR_HIGH(GPIOH_PIN8) |             \
                                     PIN_ODR_HIGH(GPIOH_PIN9) |             \
                                     PIN_ODR_HIGH(GPIOH_PIN10) |            \
                                     PIN_ODR_HIGH(GPIOH_PIN11) |            \
                                     PIN_ODR_HIGH(GPIOH_PIN12) |            \
                                     PIN_ODR_HIGH(GPIOH_PIN13) |            \
                                     PIN_ODR_HIGH(GPIOH_PIN14) |            \
                                     PIN_ODR_HIGH(GPIOH_PIN15))
#define VAL_GPIOH_AFRL              (PIN_AFIO_AF(GPIOH_OSC_IN, 0U) |        \
                                     PIN_AFIO_AF(GPIOH_OSC_OUT, 0U) |       \
                                     PIN_AFIO_AF(GPIOH_PIN2, 0U) |          \
                                     PIN_AFIO_AF(GPIOH_PIN3, 0U) |          \
                                     PIN_AFIO_AF(GPIOH_PIN4, 0U) |          \
                                     PIN_AFIO_AF(GPIOH_PIN5, 0U) |          \
                                     PIN_AFIO_AF(GPIOH_PIN6, 0U) |          \
                                     PIN_AFIO_AF(GPIOH_PIN7, 0U))
#define VAL_GPIOH_AFRH              (PIN_AFIO_AF(GPIOH_PIN8, 0U) |          \
                                     PIN_AFIO_AF(GPIOH_PIN9, 0U) |          \
                                     PIN_AFIO_AF(GPIOH_PIN10, 0U) |         \
                                     PIN_AFIO_AF(GPIOH_PIN11, 0U) |         \
                                     PIN_AFIO_AF(GPIOH_PIN12, 0U) |         \
                                     PIN_AFIO_AF(GPIOH_PIN13, 0U) |         \
                                     PIN_AFIO_AF(GPIOH_PIN14, 0U) |         \
                                     PIN_AFIO_AF(GPIOH_PIN15, 0U))
#define VAL_GPIOH_ASCR              (PIN_ASCR_DISABLED(GPIOH_OSC_IN) |      \
                                     PIN_ASCR_DISABLED(GPIOH_OSC_OUT) |     \
                                     PIN_ASCR_DISABLED(GPIOH_PIN2) |        \
                                     PIN_ASCR_DISABLED(GPIOH_PIN3) |        \
                                     PIN_ASCR_DISABLED(GPIOH_PIN4) |        \
                                     PIN_ASCR_DISABLED(GPIOH_PIN5) |        \
                                     PIN_ASCR_DISABLED(GPIOH_PIN6) |        \
                                     PIN_ASCR_DISABLED(GPIOH_PIN7) |        \
                                     PIN_ASCR_DISABLED(GPIOH_PIN8) |        \
                                     PIN_ASCR_DISABLED(GPIOH_PIN9) |        \
                                     PIN_ASCR_DISABLED(GPIOH_PIN10) |       \
                                     PIN_ASCR_DISABLED(GPIOH_PIN11) |       \
                                     PIN_ASCR_DISABLED(GPIOH_PIN12) |       \
                                     PIN_ASCR_DISABLED(GPIOH_PIN13) |       \
                                     PIN_ASCR_DISABLED(GPIOH_PIN14) |       \
                                     PIN_ASCR_DISABLED(GPIOH_PIN15))
#define VAL_GPIOH_LOCKR             (PIN_LOCKR_DISABLED(GPIOH_OSC_IN) |     \
                                     PIN_LOCKR_DISABLED(GPIOH_OSC_OUT) |    \
                                     PIN_LOCKR_DISABLED(GPIOH_PIN2) |       \
                                     PIN_LOCKR_DISABLED(GPIOH_PIN3) |       \
                                     PIN_LOCKR_DISABLED(GPIOH_PIN4) |       \
                                     PIN_LOCKR_DISABLED(GPIOH_PIN5) |       \
                                     PIN_LOCKR_DISABLED(GPIOH_PIN6) |       \
                                     PIN_LOCKR_DISABLED(GPIOH_PIN7) |       \
                                     PIN_LOCKR_DISABLED(GPIOH_PIN8) |       \
                                     PIN_LOCKR_DISABLED(GPIOH_PIN9) |       \
                                     PIN_LOCKR_DISABLED(GPIOH_PIN10) |      \
                                     PIN_LOCKR_DISABLED(GPIOH_PIN11) |      \
                                     PIN_LOCKR_DISABLED(GPIOH_PIN12) |      \
                                     PIN_LOCKR_DISABLED(GPIOH_PIN13) |      \
                                     PIN_LOCKR_DISABLED(GPIOH_PIN14) |      \
                                     PIN_LOCKR_DISABLED(GPIOH_PIN15))

/*===========================================================================*/
/* External declarations.                                                    */
/*===========================================================================*/

#if !defined(_FROM_ASM_)
#ifdef __cplusplus
extern "C" {
#endif
  void boardInit(void);
#ifdef __cplusplus
}
#endif
#endif /* _FROM_ASM_ */

#endif /* BOARD_H */
