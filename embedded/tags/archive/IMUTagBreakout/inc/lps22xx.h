/**
 * @file lps22xx.h
 * @brief Legacy IMUTagBreakout LPS22HH/LPS22DF pressure sensor API.
 * @author tag firmware authors
 * @date 2026-05-23
 *
 * The platform provides spi_write() and spi_read(), while this driver owns
 * register definitions, bit masks, mode enums, and device-level helpers.
 */

#ifndef LPS22XX_H
#define LPS22XX_H

#include <stdint.h>
#include <stdbool.h>

/** @brief Write bytes to the platform pressure-sensor SPI path. */
int spi_write(uint8_t reg, uint8_t *data, uint16_t len);
/** @brief Read bytes from the platform pressure-sensor SPI path. */
int spi_read(uint8_t reg, uint8_t *data, uint16_t len);

/* =====================================================
 * ===================== LPS22HH =======================
 * =====================================================
 */

/* WHO_AM_I value for the LPS22HH device. */
#define LPS22HH_WHO_AM_I_VAL    0xB3

/* Register map subset used by this driver. */
#define LPS22HH_REG_WHO_AM_I         0x0F
#define LPS22HH_REG_CTRL_REG1        0x10
#define LPS22HH_REG_CTRL_REG2        0x11
#define LPS22HH_REG_CTRL_REG3        0x12
#define LPS22HH_REG_STATUS           0x27
#define LPS22HH_REG_PRESS_OUT_XL     0x28

/* CTRL_REG1 bit definitions. */
#define LPS22HH_CTRL1_ODR_MASK       (0x07u << 4) /* ODR[2:0] */
#define LPS22HH_CTRL1_EN_LPFP        (1u << 3)    /* low-pass filter enable */
#define LPS22HH_CTRL1_LPFP_CFG_MASK  (0x03u << 2) /* low-pass filter config */
#define LPS22HH_CTRL1_BDU            (1u << 1)    /* block data update */
#define LPS22HH_CTRL1_SIM            (1u << 0)    /* SPI 3-wire mode; unused */

/* CTRL_REG2 bit definitions. */
#define LPS22HH_CTRL2_BOOT           (1u << 7)
#define LPS22HH_CTRL2_INT_H_L        (1u << 6)
#define LPS22HH_CTRL2_PP_OD          (1u << 5)
#define LPS22HH_CTRL2_IF_ADD_INC     (1u << 4) /* auto-increment for SPI/I2C */
#define LPS22HH_CTRL2_SWRESET        (1u << 2)
#define LPS22HH_CTRL2_LOW_NOISE_EN   (1u << 1)
#define LPS22HH_CTRL2_ONE_SHOT       (1u << 0)

/* CTRL_REG3 bit definitions. */
#define LPS22HH_CTRL3_INT_F_FULL     (1u << 2)
#define LPS22HH_CTRL3_INT_F_WTM      (1u << 3)
#define LPS22HH_CTRL3_INT_F_OVR      (1u << 4)
#define LPS22HH_CTRL3_DRDY           (1u << 5) /* data-ready routed to INT/DRDY */
#define LPS22HH_CTRL3_INT_S0         (1u << 6)
#define LPS22HH_CTRL3_INT_S1         (1u << 7)
#define LPS22HH_CTRL3_INT_S_MASK     (LPS22HH_CTRL3_INT_S0 | LPS22HH_CTRL3_INT_S1)

/* STATUS register flags. */
#define LPS22HH_STATUS_P_DA          (1u << 0) /* pressure data available */
#define LPS22HH_STATUS_T_DA          (1u << 1) /* temperature data available */

/*
 * Output data rate selection.
 *
 * The enum values are chosen to map directly into the ODR field encoding used
 * by CTRL_REG1. Value 0 is power-down / one-shot arming state.
 */
typedef enum {
    LPS22HH_ODR_ONE_SHOT = 0,
    LPS22HH_ODR_1HZ      = 1,
    LPS22HH_ODR_10HZ     = 2,
    LPS22HH_ODR_25HZ     = 3,
    LPS22HH_ODR_50HZ     = 4,
    LPS22HH_ODR_75HZ     = 5,
    LPS22HH_ODR_100HZ    = 6,
    LPS22HH_ODR_200HZ    = 7
} lps22hh_odr_t;

/*
 * LPS22HH low-pass filter selection.
 *
 * Note: the datasheet defines the filter options through EN_LPFP and LPFP_CFG.
 * A value of DISABLE means the filter path is bypassed.
 */
typedef enum {
    LPS22HH_LPF_DISABLE    = 0,
    LPS22HH_LPF_ODR_DIV_9  = 0,
    LPS22HH_LPF_ODR_DIV_20 = 1
} lps22hh_lpf_t;

/** @brief Check the LPS22HH identity register. */
bool lps22hh_check_who_am_i(void);
/** @brief Put the LPS22HH into idle mode. */
int  lps22hh_set_idle(void);
/** @brief Configure the LPS22HH for continuous sampling. */
int  lps22hh_config_continuous(lps22hh_odr_t odr, lps22hh_lpf_t lpf);
/** @brief Configure the LPS22HH for one-shot sampling. */
int  lps22hh_config_triggered(lps22hh_lpf_t lpf);
/** @brief Trigger one LPS22HH one-shot conversion. */
int  lps22hh_trigger_one_shot(void);
/** @brief Check whether LPS22HH sample data is ready. */
bool lps22hh_data_ready(void);
/** @brief Read raw LPS22HH pressure and temperature data. */
int  lps22hh_read_raw(int32_t *pressure, int32_t *temperature);

/* =====================================================
 * ===================== LPS22DF =======================
 * =====================================================
 */

/* WHO_AM_I value for the LPS22DF device. */
#define LPS22DF_WHO_AM_I_VAL    0xB4

/* Register map subset used by this driver. */
#define LPS22DF_REG_WHO_AM_I         0x0F
#define LPS22DF_REG_CTRL_REG1        0x10
#define LPS22DF_REG_CTRL_REG2        0x11
#define LPS22DF_REG_CTRL_REG3        0x12
#define LPS22DF_REG_CTRL_REG4        0x13
#define LPS22DF_REG_STATUS           0x27
#define LPS22DF_REG_PRESS_OUT_XL     0x28

/* CTRL_REG1 bit definitions for ODR and averaging. */
#define LPS22DF_CTRL1_ODR_MASK       (0x0Fu << 3) /* ODR[3:0] */
#define LPS22DF_CTRL1_AVG_MASK       (0x07u << 0) /* averaging selection */

/* CTRL_REG2 bit definitions. */
#define LPS22DF_CTRL2_BOOT           (1u << 7)
#define LPS22DF_CTRL2_LFPF_CFG       (1u << 5)
#define LPS22DF_CTRL2_EN_LPFP        (1u << 4)
#define LPS22DF_CTRL2_BDU            (1u << 3)
#define LPS22DF_CTRL2_SWRESET        (1u << 2)
#define LPS22DF_CTRL2_ONESHOT        (1u << 0)

/* CTRL_REG3 bit definitions. */
#define LPS22DF_CTRL3_INT_H_L        (1u << 4)
#define LPS22DF_CTRL3_PP_OD          (1u << 2)
#define LPS22DF_CTRL3_IF_ADD_INC     (1u << 0)

/* CTRL_REG4 bit definitions. */
#define LPS22DF_CTRL4_DRDY_PLS       (1u << 6)
#define LPS22DF_CTRL4_DRDY           (1u << 5) /* data-ready routed to INT/DRDY */
#define LPS22DF_CTRL4_INT_EN         (1u << 4)
#define LPS22DF_CTRL4_INT_F_FULL     (1u << 2)
#define LPS22DF_CTRL4_INT_F_WTM      (1u << 1)
#define LPS22DF_CTRL4_INT_F_OVR      (1u << 0)

/* STATUS register flags. */
#define LPS22DF_STATUS_P_DA          (1u << 0)
#define LPS22DF_STATUS_T_DA          (1u << 1)

/*
 * LPS22DF output data rate selection.
 *
 * Value 0 corresponds to one-shot / power-down arming state. The remaining
 * values map to the device ODR field encoding.
 */
typedef enum {
    LPS22DF_ODR_ONE_SHOT = 0,
    LPS22DF_ODR_1HZ      = 1,
    LPS22DF_ODR_4HZ      = 2,
    LPS22DF_ODR_10HZ     = 3,
    LPS22DF_ODR_25HZ     = 4,
    LPS22DF_ODR_50HZ     = 5,
    LPS22DF_ODR_75HZ     = 6,
    LPS22DF_ODR_100HZ    = 7,
    LPS22DF_ODR_200HZ    = 8
} lps22df_odr_t;

/*
 * LPS22DF low-pass filter selection.
 *
 * The driver interprets DISABLE as the default filter path and ODR_DIV_9 as
 * the alternate filter selection described in the datasheet.
 */
typedef enum {
    LPS22DF_LPF_DISABLE   = 0,
    LPS22DF_LPF_ODR_DIV_9 = 1
} lps22df_lpf_t;

/*
 * LPS22DF averaging / oversampling selection.
 *
 * These values map directly into CTRL_REG1.AVG[2:0].
 */
typedef enum {
    LPS22DF_AVG_4   = 0,
    LPS22DF_AVG_8   = 1,
    LPS22DF_AVG_16  = 2,
    LPS22DF_AVG_32  = 3,
    LPS22DF_AVG_64  = 4,
    LPS22DF_AVG_128 = 5,
    LPS22DF_AVG_512 = 7
} lps22df_avg_t;

/** @brief Check the LPS22DF identity register. */
bool lps22df_check_who_am_i(void);
/** @brief Put the LPS22DF into idle mode. */
int  lps22df_set_idle(void);
/** @brief Configure the LPS22DF for continuous sampling. */
int  lps22df_config_continuous(lps22df_odr_t odr, lps22df_lpf_t lpf, lps22df_avg_t avg);
/** @brief Configure the LPS22DF for one-shot sampling. */
int  lps22df_config_triggered(lps22df_lpf_t lpf, lps22df_avg_t avg);
/** @brief Trigger one LPS22DF one-shot conversion. */
int  lps22df_trigger_one_shot(void);
/** @brief Check whether LPS22DF sample data is ready. */
bool lps22df_data_ready(void);
/** @brief Read raw LPS22DF pressure and temperature data. */
int  lps22df_read_raw(int32_t *pressure, int32_t *temperature);

#endif /* LPS22XX_H */
