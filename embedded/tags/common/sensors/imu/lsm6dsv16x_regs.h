/**
 * @file    lsm6dsv16x_regs.h
 * @brief   LSM6DSV16X internal register map, bit masks, and driver-private
 *          enumerations.
 *
 * THIS FILE IS NOT PART OF THE PUBLIC API.
 * Include it only from lsm6dsv16x.c.  Application code must not include
 * it directly; everything an application needs is in lsm6dsv16x.h.
 *
 * Register addresses and bit definitions are taken from:
 *   [DS]  LSM6DSV16X Datasheet DS13726 rev 5
 *   [AN]  Application note AN5763
 *   [DT]  Design tip DT0155
 */

#ifndef LSM6DSV16X_REGS_H
#define LSM6DSV16X_REGS_H

#include <stdint.h>
#include <stddef.h>
#include "lsm6dsv16x.h"   /* for lsm6dsv16x_bdr_t and other shared types  */

/* =========================================================================
 * Register map – primary interface registers
 * ====================================================================== */
#define LSM6DSV16X_FUNC_CFG_ACCESS    0x01U  /* Embedded-function bank access     */
#define LSM6DSV16X_FIFO_CTRL1         0x07U  /* FIFO watermark [7:0]              */
#define LSM6DSV16X_FIFO_CTRL2         0x08U  /* FIFO watermark [8], mode flags    */
#define LSM6DSV16X_FIFO_CTRL3         0x09U  /* Batch data rates (XL, G)          */
#define LSM6DSV16X_FIFO_CTRL4         0x0AU  /* FIFO mode select                  */
#define LSM6DSV16X_COUNTER_BDR_REG1   0x0BU  /* Batch counter / ODR_TRIG MSB     */
#define LSM6DSV16X_COUNTER_BDR_REG2   0x0CU  /* Batch counter register 2          */
#define LSM6DSV16X_INT1_CTRL          0x0DU  /* INT1 data-ready / FIFO routing    */
#define LSM6DSV16X_INT2_CTRL          0x0EU  /* INT2 data-ready / FIFO routing    */
#define LSM6DSV16X_WHO_AM_I           0x0FU  /* Device identity; expected: 0x70   */
#define LSM6DSV16X_CTRL1              0x10U  /* Accelerometer: OP_MODE, ODR       */
#define LSM6DSV16X_CTRL2              0x11U  /* Gyroscope: OP_MODE, ODR           */
#define LSM6DSV16X_CTRL3              0x12U  /* BOOT, BDU, IF_INC, SW_RESET       */
#define LSM6DSV16X_CTRL4              0x13U  /* INT2 edge polarity, DRDY_MASK     */
#define LSM6DSV16X_CTRL5              0x14U  /* Bus activity / I3C configuration  */
#define LSM6DSV16X_CTRL6              0x15U  /* LPF1_G_BW[2:0], FS_G[3:0]        */
#define LSM6DSV16X_CTRL7              0x16U  /* LPF1_G_EN, AH_QVAR               */
#define LSM6DSV16X_CTRL8              0x17U  /* HP_LPF2_XL_BW[2:0], FS_XL[1:0]  */
#define LSM6DSV16X_CTRL9              0x18U  /* ODR_TRIG_EN, HP_REF_MODE_XL      */
#define LSM6DSV16X_CTRL10             0x19U  /* Accel/gyro self-test, timestamp   */
#define LSM6DSV16X_ALL_INT_SRC        0x1AU  /* Combined interrupt source status  */
#define LSM6DSV16X_WAKE_UP_SRC        0x1BU  /* Wake-up event source register     */
#define LSM6DSV16X_STATUS_REG         0x1EU  /* XLDA, GDA, TDA data-ready flags   */
#define LSM6DSV16X_OUTX_L_G           0x22U  /* Gyroscope X output LSB            */
#define LSM6DSV16X_OUTX_L_A           0x28U  /* Accelerometer X output LSB        */
#define LSM6DSV16X_FIFO_STATUS1       0x3AU  /* DIFF_FIFO[7:0]                    */
#define LSM6DSV16X_FIFO_STATUS2       0x3BU  /* FIFO_WTM_IA, OVR_IA, DIFF_FIFO[9:8] */
#define LSM6DSV16X_EMB_FUNC_EN_B      0x05U  /* Embedded bank: FIFO_COMPR_EN      */
#define LSM6DSV16X_TAP_CFG0           0x56U  /* LIR, SLOPE_FDS filter select      */
#define LSM6DSV16X_TAP_CFG2           0x58U  /* INTERRUPTS_ENABLE master bit      */
#define LSM6DSV16X_WAKE_UP_THS        0x5BU  /* WK_THS[5:0], SLEEP_ON             */
#define LSM6DSV16X_WAKE_UP_DUR        0x5CU  /* SLEEP_DUR[3:0], WAKE_DUR[1:0]    */
#define LSM6DSV16X_MD1_CFG            0x5EU  /* INT1 embedded-function routing    */
#define LSM6DSV16X_MD2_CFG            0x5FU  /* INT2 embedded-function routing    */
#define LSM6DSV16X_HAODR_CFG          0x62U  /* HAODR_SEL[1:0]                    */
#define LSM6DSV16X_ODR_TRIG_CFG       0x6DU  /* ODR_TRIG_N_ODR[7:0] (low byte)   */
#define LSM6DSV16X_FIFO_DATA_OUT_TAG  0x78U  /* Tag byte of next FIFO word        */
#define LSM6DSV16X_FIFO_DATA_OUT_X_L  0x79U  /* FIFO data word X LSB              */

/* =========================================================================
 * WHO_AM_I expected value
 * ====================================================================== */
#define LSM6DSV16X_WHOAMI_VALUE       0x70U

/* =========================================================================
 * CTRL3 (0x12) bit masks
 * ====================================================================== */
#define LSM6DSV16X_CTRL3_SW_RESET     0x01U  /* Software reset (self-clearing)    */
#define LSM6DSV16X_CTRL3_IF_INC       0x04U  /* Auto-increment register address   */
#define LSM6DSV16X_CTRL3_PP_OD        0x10U  /* Interrupt pins: 0=push-pull, 1=OD */
#define LSM6DSV16X_CTRL3_BDU          0x40U  /* Block data update                 */
#define LSM6DSV16X_CTRL3_BOOT         0x80U  /* Reboot memory content             */

/* =========================================================================
 * CTRL4 (0x13) bit masks
 * ====================================================================== */
#define LSM6DSV16X_CTRL4_INT2_IN_LH   0x01U  /* INT2 edge: 0=falling, 1=rising    */
#define LSM6DSV16X_CTRL4_DRDY_PULSED  0x02U  /* DRDY signal mode: 0=latched       */
#define LSM6DSV16X_CTRL4_DRDY_MASK    0x08U  /* Mask DRDY until filter settles    */
#define LSM6DSV16X_CTRL4_INT2_ON_INT1 0x20U  /* Merge INT2 signals onto INT1 pin  */

/* =========================================================================
 * CTRL7 (0x16) bit masks
 * ====================================================================== */
#define LSM6DSV16X_CTRL7_LPF1_G_EN    0x01U  /* Enable gyroscope LPF1             */

/* =========================================================================
 * CTRL9 (0x18) bit masks
 * ====================================================================== */
#define LSM6DSV16X_CTRL9_ODR_TRIG_EN  0x80U  /* Enable ODR-triggered sampling     */

/* =========================================================================
 * CTRL10 (0x19) bit masks – accelerometer self-test control
 *
 * ST_XL occupies the low two bits.  The gyroscope self-test field is separate
 * and is intentionally not used by this accelerometer self-test driver path.
 * ====================================================================== */
#define LSM6DSV16X_CTRL10_ST_XL_MASK  0x03U  /* ST_XL[1:0] field mask             */
#define LSM6DSV16X_CTRL10_ST_XL_OFF   0x00U  /* Self-test disabled                */
#define LSM6DSV16X_CTRL10_ST_XL_POS   0x01U  /* Positive self-test                */
#define LSM6DSV16X_CTRL10_ST_XL_NEG   0x02U  /* Negative self-test                */

/* =========================================================================
 * INT1_CTRL (0x0D) bit masks
 * ====================================================================== */
#define LSM6DSV16X_INT1_DRDY_XL       0x01U  /* Accel data-ready on INT1          */
#define LSM6DSV16X_INT1_DRDY_G        0x02U  /* Gyro data-ready on INT1           */
#define LSM6DSV16X_INT1_FIFO_TH       0x08U  /* FIFO watermark reached on INT1    */
#define LSM6DSV16X_INT1_FIFO_OVR      0x10U  /* FIFO overrun on INT1              */
#define LSM6DSV16X_INT1_FIFO_FULL     0x20U  /* FIFO full on INT1                 */

/* =========================================================================
 * MD1_CFG (0x5E) bit masks – INT1 embedded-function routing
 * ====================================================================== */
#define LSM6DSV16X_MD1_INT1_WU            0x20U  /* Wake-up event → INT1          */
#define LSM6DSV16X_MD1_INT1_SLEEP_CHANGE  0x80U  /* Stationary/motion change → INT1 */

/* =========================================================================
 * MD2_CFG (0x5F) bit masks – INT2 embedded-function routing
 * ====================================================================== */
#define LSM6DSV16X_MD2_INT2_SLEEP_CHANGE  0x80U  /* Stationary/motion change → INT2 */

/* =========================================================================
 * TAP_CFG0 (0x56) bit masks
 * ====================================================================== */
#define LSM6DSV16X_TAP_CFG0_LIR          0x01U  /* Latch interrupt until src read  */
#define LSM6DSV16X_TAP_CFG0_SLOPE_FDS    0x10U  /* 0=slope filter, 1=HP filter     */

/* =========================================================================
 * TAP_CFG2 (0x58) bit masks
 * ====================================================================== */
#define LSM6DSV16X_TAP_CFG2_INTR_EN      0x80U  /* Master embedded interrupt enable */

/* =========================================================================
 * FIFO_CTRL4 (0x0A) mode values
 * ====================================================================== */
#define LSM6DSV16X_FIFO_MODE_BYPASS       0x00U  /* FIFO disabled / flushed        */
#define LSM6DSV16X_FIFO_MODE_FIFO         0x01U  /* Stop-when-full mode            */
#define LSM6DSV16X_FIFO_MODE_STREAM       0x06U  /* Continuous / overwrite mode    */

/* =========================================================================
 * FIFO_DATA_OUT_TAG (0x78) sensor-tag values (bits [7:3] of tag byte)
 * ====================================================================== */
#define LSM6DSV16X_FIFO_TAG_GYRO_NC       0x01U  /* Gyroscope, non-compressed      */
#define LSM6DSV16X_FIFO_TAG_ACCEL_NC      0x02U  /* Accelerometer, non-compressed  */
#define LSM6DSV16X_FIFO_TAG_TEMPERATURE   0x03U  /* Temperature sensor             */
#define LSM6DSV16X_FIFO_TAG_TIMESTAMP     0x04U  /* Timestamp counter word         */

/* =========================================================================
 * Gyroscope raw ODR register codes (CTRL2 ODR_G[3:0] field)
 *
 * In ODR-triggered mode these are the "gate" clock codes written to CTRL2.
 * Application code always selects the gyro rate indirectly through
 * lsm6dsv16x_trig_odr_t; these raw codes are only needed inside the
 * driver's ODR lookup table.
 * ====================================================================== */
typedef enum {
    LSM6DSV16X_G_ODR_POWER_DOWN    = 0x00,
    LSM6DSV16X_G_ODR_7Hz5          = 0x02,
    LSM6DSV16X_G_ODR_15Hz          = 0x03,
    LSM6DSV16X_G_ODR_30Hz          = 0x04,
    LSM6DSV16X_G_ODR_60Hz          = 0x05,
    LSM6DSV16X_G_ODR_120Hz         = 0x06,
    LSM6DSV16X_G_ODR_240Hz         = 0x07,
    LSM6DSV16X_G_ODR_480Hz         = 0x08,
    LSM6DSV16X_G_ODR_960Hz         = 0x09,
    LSM6DSV16X_G_ODR_1920Hz        = 0x0A,
    LSM6DSV16X_G_ODR_3840Hz        = 0x0B,
    LSM6DSV16X_G_ODR_7680Hz        = 0x0C,
} lsm6dsv16x_g_odr_t;

/* =========================================================================
 * Accelerometer OP_MODE field (CTRL1 bits [6:4])
 *
 * Selects high-performance, low-power, or ODR-triggered sampling for the
 * accelerometer.  Written by the driver init functions; never exposed to
 * application code.
 * ====================================================================== */
typedef enum {
    LSM6DSV16X_XL_OP_HP        = 0x00,  /* High-performance (default)          */
    LSM6DSV16X_XL_OP_LP        = 0x02,  /* Low-power                           */
    LSM6DSV16X_XL_OP_TRIGGERED = 0x04,  /* ODR-triggered                       */
} lsm6dsv16x_xl_op_mode_t;

/* =========================================================================
 * Gyroscope OP_MODE field (CTRL2 bits [6:4])
 * ====================================================================== */
typedef enum {
    LSM6DSV16X_G_OP_HP         = 0x00,  /* High-performance (default)          */
    LSM6DSV16X_G_OP_LP         = 0x02,  /* Low-power                           */
    LSM6DSV16X_G_OP_TRIGGERED  = 0x04,  /* ODR-triggered                       */
} lsm6dsv16x_g_op_mode_t;

/* =========================================================================
 * FIFO batch data rate codes (FIFO_CTRL3 BDR_XL[3:0] / BDR_GY[7:4])
 *
 * Set to match the sensor ODR so that every sample is batched into the FIFO.
 * Only used inside the driver; not exposed via the public API.
 * ====================================================================== */
typedef enum {
    LSM6DSV16X_BDR_NOT_BATCHED  = 0x00,
    LSM6DSV16X_BDR_1Hz875       = 0x01,
    LSM6DSV16X_BDR_7Hz5         = 0x02,
    LSM6DSV16X_BDR_15Hz         = 0x03,
    LSM6DSV16X_BDR_30Hz         = 0x04,
    LSM6DSV16X_BDR_60Hz         = 0x05,
    LSM6DSV16X_BDR_120Hz        = 0x06,
    LSM6DSV16X_BDR_240Hz        = 0x07,
    LSM6DSV16X_BDR_480Hz        = 0x08,
    LSM6DSV16X_BDR_960Hz        = 0x09,
    LSM6DSV16X_BDR_1920Hz       = 0x0A,
    LSM6DSV16X_BDR_3840Hz       = 0x0B,
    LSM6DSV16X_BDR_7680Hz       = 0x0C,
} lsm6dsv16x_bdr_t;

/* =========================================================================
 * ODR-triggered mode: internal lookup table type
 *
 * Each entry fully describes one supported trigger ODR: the gate clock code
 * to write to CTRL1/CTRL2, the multiplier S for ODR_TRIG_N_ODR, the FIFO
 * BDR code, and the MCU timer divisor D.
 * ====================================================================== */

/** Multiplier S = 25 is used for all five standard ODR targets.
 *  25 = 0x019; MSB (bit 8) = 0, so only the low byte reg 0x6D needs writing. */
#define LSM6DSV16X_ODR_TRIG_N_ODR_S25   0x19U

typedef struct {
    uint16_t         hz;         /**< Target ODR in Hz (matches trig_odr_t enum)  */
    uint8_t          odr_code;   /**< CTRL1[3:0] and CTRL2[3:0] gate clock code   */
    uint16_t         n_odr;      /**< ODR_TRIG_N_ODR value (multiplier S, 8–510)  */
    lsm6dsv16x_bdr_t bdr;        /**< FIFO batch data rate code                   */
    uint16_t         mcu_div_d;  /**< MCU timer divisor: f_mcu = 1024 / mcu_div_d */
} odr_trig_entry_t;

/* =========================================================================
 * Accelerometer self-test internal constants
 *
 * Limits from DS13726 Table 46, ±4 g full scale, sensitivity 0.122 mg/LSB.
 * ====================================================================== */
#define LSM6DSV16X_ST_XL_MIN_LSB        ((int32_t)410)   /*  50 mg / 0.122 mg/LSB ≈  410 */
#define LSM6DSV16X_ST_XL_MAX_LSB        ((int32_t)13934) /* 1700 mg / 0.122 mg/LSB ≈ 13934 */
#define LSM6DSV16X_ST_N_AVG             5U     /* Samples to average per phase  */
#define LSM6DSV16X_ST_DRDY_TIMEOUT_MS   100U   /* Max ms to wait for XLDA       */

#endif /* LSM6DSV16X_REGS_H */
