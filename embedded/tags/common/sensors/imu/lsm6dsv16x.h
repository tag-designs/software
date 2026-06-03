/**
 * @file    lsm6dsv16x.h
 * @brief   Public API for the LSM6DSV16X 6-axis IMU driver.
 *
 * This header is the only file application code needs to include.
 * Register addresses, bit masks, and other hardware details are kept in
 * lsm6dsv16x_regs.h, which is included only by lsm6dsv16x.c.
 *
 * Operational modes
 * -----------------
 *   MODE 0  SHUTDOWN        - Both sensors powered down, FIFO disabled,
 *                             external ODR trigger disabled.
 *   MODE 1  ACCEL_ONLY      - Accelerometer, high-performance, internal ODR.
 *                             Data-ready on INT1.
 *   MODE 2  ACCEL_WAKEUP    - Accelerometer, low-power, slope-filter wakeup.
 *                             Wakeup interrupt on INT1.
 *   MODE 3  ACCEL_GYRO_TRIG - Accel + Gyro, external ODR trigger on INT2,
 *                             FIFO streaming, stationary/motion on INT1.
 *
 * Filtering policy
 * ----------------
 * All modes apply Nyquist-minimum filtering only:
 *   Accelerometer: LPF1 at ODR/2 (reset default, cannot be disabled).
 *                  LPF2 is disabled (LPF2_XL_EN = 0).
 *   Gyroscope:     Optional LPF1 disabled (LPF1_G_EN = 0).
 *                  Only the inherent ADC anti-alias response (ODR/2) is active.
 * Filter parameters are not exposed in the API; they are fixed by the driver.
 *
 * Device model
 * ------------
 * Concrete tag or family code provides a TagLsm6dsv16xDevice descriptor that
 * binds register IO and optional ODR-trigger timer control.
 *
 * The descriptor's TagRegisterDevice owns bus selection, chip select, power
 * line, and sleep policy.  Public driver calls bracket register transactions
 * with tagBusPowerOn()/tagBusBegin()/tagBusEnd().  Active-mode initializers
 * leave the sensor powered so later reads can succeed; shutdown and self-test
 * leave the sensor powered down.
 *
 * The trigger callback is required only for MODE 3. Tags that do not wire the
 * LSM6DSV16X external ODR trigger may set it to NULL and still use shutdown,
 * accel-only, wakeup, direct-read, and accelerometer self-test paths.
 *
 * Register references
 * -------------------
 *   DS13726 rev 5 - LSM6DSV16X datasheet
 *   AN5763        - LSM6DSV16X application note
 *   DT0155        - ODR-triggered mode design tip
 */

#ifndef LSM6DSV16X_H
#define LSM6DSV16X_H

#include <stddef.h>
#include <stdint.h>

#include "sensor_io.h"

/**
 * @brief Configure the MCU hardware timer used as the external ODR trigger.
 *
 * Passing divider 0 disables the trigger output. Nonzero values configure the
 * output frequency as 1024 / divider Hz. The callback belongs to the tag or
 * family because the timer, pin, and low-power behavior are board-specific.
 */
typedef void (*TagLsm6dsv16xSetTrigger)(const void *context,
                                        unsigned int divider);

/**
 * @brief Concrete LSM6DSV16X hardware binding supplied by a tag/family.
 */
typedef struct {
    const TagRegisterDevice *registers;       /**< Required register-device descriptor. */
    TagLsm6dsv16xSetTrigger set_trigger;      /**< Optional external-ODR trigger callback. */
    const void *trigger_context;              /**< Optional context passed to set_trigger. */
} TagLsm6dsv16xDevice;

/* =========================================================================
 * Accelerometer ODR
 * Numeric values are the CTRL1 ODR_XL[3:0] register codes.
 * ====================================================================== */
typedef enum {
    LSM6DSV16X_XL_ODR_POWER_DOWN = 0x00,
    LSM6DSV16X_XL_ODR_1Hz875     = 0x01,
    LSM6DSV16X_XL_ODR_7Hz5       = 0x02,
    LSM6DSV16X_XL_ODR_15Hz       = 0x03,
    LSM6DSV16X_XL_ODR_30Hz       = 0x04,
    LSM6DSV16X_XL_ODR_60Hz       = 0x05,
    LSM6DSV16X_XL_ODR_120Hz      = 0x06,
    LSM6DSV16X_XL_ODR_240Hz      = 0x07,
    LSM6DSV16X_XL_ODR_480Hz      = 0x08,
    LSM6DSV16X_XL_ODR_960Hz      = 0x09,
    LSM6DSV16X_XL_ODR_1920Hz     = 0x0A,
    LSM6DSV16X_XL_ODR_3840Hz     = 0x0B,
    LSM6DSV16X_XL_ODR_7680Hz     = 0x0C,
} lsm6dsv16x_xl_odr_t;

/* =========================================================================
 * Accelerometer full-scale range
 * ====================================================================== */
typedef enum {
    LSM6DSV16X_XL_FS_2G  = 0x00, /**< +/-2  g  0.061 mg/LSB */
    LSM6DSV16X_XL_FS_4G  = 0x01, /**< +/-4  g  0.122 mg/LSB */
    LSM6DSV16X_XL_FS_8G  = 0x02, /**< +/-8  g  0.244 mg/LSB */
    LSM6DSV16X_XL_FS_16G = 0x03, /**< +/-16 g  0.488 mg/LSB */
} lsm6dsv16x_xl_fs_t;

/* =========================================================================
 * Gyroscope full-scale range
 * ====================================================================== */
typedef enum {
    LSM6DSV16X_G_FS_125DPS  = 0x00, /**< +/-125  dps  4.375 mdps/LSB */
    LSM6DSV16X_G_FS_250DPS  = 0x01, /**< +/-250  dps  8.75  mdps/LSB */
    LSM6DSV16X_G_FS_500DPS  = 0x02, /**< +/-500  dps  17.5  mdps/LSB */
    LSM6DSV16X_G_FS_1000DPS = 0x03, /**< +/-1000 dps  35    mdps/LSB */
    LSM6DSV16X_G_FS_2000DPS = 0x04, /**< +/-2000 dps  70    mdps/LSB */
    LSM6DSV16X_G_FS_4000DPS = 0x0C, /**< +/-4000 dps  140   mdps/LSB */
} lsm6dsv16x_g_fs_t;

/* =========================================================================
 * Mode 3 external trigger ODR
 *
 * The MCU timer base clock is 1024 Hz.  All five ODRs are achieved exactly
 * by the internal sensor multiplier S = 25:   f_out = 25 x (1024 / D)
 * where D is the MCU timer divisor managed internally by the driver and
 * passed to the descriptor trigger callback.
 * ====================================================================== */
typedef enum {
    LSM6DSV16X_TRIG_ODR_50HZ   =   50, /**< D=512, f_mcu=  2 Hz, S=25 */
    LSM6DSV16X_TRIG_ODR_100HZ  =  100, /**< D=256, f_mcu=  4 Hz, S=25 */
    LSM6DSV16X_TRIG_ODR_200HZ  =  200, /**< D=128, f_mcu=  8 Hz, S=25 */
    LSM6DSV16X_TRIG_ODR_400HZ  =  400, /**< D= 64, f_mcu= 16 Hz, S=25 */
    LSM6DSV16X_TRIG_ODR_800HZ  =  800, /**< D= 32, f_mcu= 32 Hz, S=25 */
    LSM6DSV16X_TRIG_ODR_1024HZ = 1024, /**< D=  1, f_mcu=1024 Hz, S=1 */
} lsm6dsv16x_trig_odr_t;

/* =========================================================================
 * Wakeup / motion detection enumerations
 * ====================================================================== */

/** Wakeup threshold; 1 LSB = FS_XL/64 (values below assume default +/-2 g). */
typedef enum {
    LSM6DSV16X_WK_THS_31mg   = 0x01,
    LSM6DSV16X_WK_THS_63mg   = 0x02,
    LSM6DSV16X_WK_THS_125mg  = 0x04,
    LSM6DSV16X_WK_THS_250mg  = 0x08,
    LSM6DSV16X_WK_THS_500mg  = 0x10,
    LSM6DSV16X_WK_THS_1000mg = 0x20,
} lsm6dsv16x_wk_ths_t;

/** Consecutive above-threshold samples required before wakeup is declared. */
typedef enum {
    LSM6DSV16X_WK_DUR_0 = 0x00,
    LSM6DSV16X_WK_DUR_1 = 0x01,
    LSM6DSV16X_WK_DUR_2 = 0x02,
    LSM6DSV16X_WK_DUR_3 = 0x03,
} lsm6dsv16x_wk_dur_t;

/** Inactivity duration before sleep transition; 1 LSB = 514/ODR_XL. */
typedef enum {
    LSM6DSV16X_SLEEP_DUR_DEFAULT = 0x00, /**< 18/ODR_XL (default) */
    LSM6DSV16X_SLEEP_DUR_1       = 0x01, /**< 514/ODR_XL          */
    LSM6DSV16X_SLEEP_DUR_2       = 0x02, /**< 1028/ODR_XL         */
    LSM6DSV16X_SLEEP_DUR_4       = 0x04, /**< 2056/ODR_XL         */
    LSM6DSV16X_SLEEP_DUR_8       = 0x08, /**< 4112/ODR_XL         */
    LSM6DSV16X_SLEEP_DUR_15      = 0x0F, /**< 7710/ODR_XL (max)   */
} lsm6dsv16x_sleep_dur_t;

/* =========================================================================
 * Self-test result
 * ====================================================================== */
typedef enum {
    LSM6DSV16X_SELF_TEST_PASS    =  0,
    LSM6DSV16X_SELF_TEST_FAIL_X  = -1,
    LSM6DSV16X_SELF_TEST_FAIL_Y  = -2,
    LSM6DSV16X_SELF_TEST_FAIL_Z  = -3,
    LSM6DSV16X_SELF_TEST_FAIL_ID = -4,
    LSM6DSV16X_SELF_TEST_FAIL_TO = -5,
} lsm6dsv16x_self_test_result_t;

/* =========================================================================
 * Data types
 * ====================================================================== */

/**
 * One synchronised sample unpacked from the FIFO.
 * Values are raw two's-complement 16-bit integers.
 * Multiply by the active full-scale sensitivity to get physical units.
 * For example, accelerometer sensitivity is 0.061 mg/LSB at +/-2 g and
 * 0.122 mg/LSB at +/-4 g. Gyroscope sensitivity is listed with the
 * lsm6dsv16x_g_fs_t full-scale options.
 */
typedef struct {
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
} lsm6dsv16x_sample_t;

/* =========================================================================
 * Configuration structures
 *
 * Filter parameters have been removed from all structures.  The driver
 * applies Nyquist-minimum filtering automatically (see Filtering policy
 * in the file header comment).
 * ====================================================================== */

/** MODE 1 - Accelerometer only, high-performance, internal ODR. */
typedef struct {
    lsm6dsv16x_xl_odr_t odr;  /**< Output data rate */
} lsm6dsv16x_accel_cfg_t;

/** MODE 2 - Accelerometer wakeup, low-power, internal ODR. */
typedef struct {
    lsm6dsv16x_xl_odr_t odr;  /**< Output data rate (low-power range) */
} lsm6dsv16x_wakeup_mode_cfg_t;

/** MODE 3 - Accel + gyro, external ODR trigger, FIFO stream. */
typedef struct {
    lsm6dsv16x_trig_odr_t trig_odr;  /**< Target sample rate (Hz) */
} lsm6dsv16x_trig_mode_cfg_t;

/** Wakeup and stationary/motion detection parameters. */
typedef struct {
    lsm6dsv16x_wk_ths_t    threshold;
    lsm6dsv16x_wk_dur_t    wakeup_dur;
    lsm6dsv16x_sleep_dur_t sleep_dur;
} lsm6dsv16x_motion_cfg_t;

/** Full-scale range for both sensors. */
typedef struct {
    lsm6dsv16x_xl_fs_t xl_fs;
    lsm6dsv16x_g_fs_t  g_fs;
} lsm6dsv16x_range_cfg_t;

/* =========================================================================
 * ODR trigger parameter query result
 *
 * Filled by lsm6dsv16x_get_trig_params().  Informational — the driver
 * programs the trigger automatically via the descriptor trigger callback.
 * Applications may call this to verify or log the computed parameters.
 * ====================================================================== */
typedef struct {
    uint16_t target_hz;      /**< Requested ODR in Hz (= enum value)          */
    uint16_t mcu_div_d;      /**< Divisor D passed to trigger callback         */
    uint32_t f_mcu_millihz;  /**< f_mcu in milli-Hz = (1024 x 1000) / D       */
    uint16_t multiplier_s;   /**< Internal sensor multiplier S (25 for all
                                   standard targets)                           */
    uint8_t  odr_trig_cfg;   /**< Low byte of S written to reg 0x6D           */
    uint8_t  cbdr_reg1_msb;  /**< MSB of S for COUNTER_BDR_REG1[5] (0 if S<256)*/
    uint8_t  ctrl_odr_code;  /**< CTRL1[3:0] / CTRL2[3:0] gate ODR code       */
    uint8_t  fifo_bdr_code;  /**< FIFO_CTRL3 BDR field code                   */
} lsm6dsv16x_trig_params_t;

/* =========================================================================
 * Public API
 * ====================================================================== */

/** Verify WHO_AM_I. Returns 0 on match, -1 on mismatch. */
int lsm6dsv16x_verify_id(const TagLsm6dsv16xDevice *device);

/**
 * MODE 0: power down both sensors, disable FIFO, disable ODR trigger.
 *
 * This call ends its bus transaction and powers the descriptor down.
 */
void lsm6dsv16x_init_shutdown(const TagLsm6dsv16xDevice *device);

/**
 * MODE 1: accelerometer HP, internal ODR, minimum filtering.
 * Data-ready on INT1. Disables the external ODR trigger.
 *
 * The sensor remains powered and configured after this call. Use
 * lsm6dsv16x_read_accel() to retrieve raw samples.
 *
 * @param device Concrete device descriptor. Must not be NULL.
 * @param cfg ODR selection. Must not be NULL.
 */
void lsm6dsv16x_init_accel_only(const TagLsm6dsv16xDevice *device,
                                const lsm6dsv16x_accel_cfg_t *cfg);

/**
 * MODE 2: accelerometer LP, slope-filter wakeup, minimum filtering.
 * Wakeup and stationary/motion events on INT1. Disables the external ODR
 * trigger.
 *
 * The sensor remains powered and configured after this call. Read
 * WAKE_UP_SRC through lsm6dsv16x_read_wakeup_src() to clear latched events.
 *
 * @param device Concrete device descriptor. Must not be NULL.
 * @param cfg ODR selection. Must not be NULL.
 * @param mot_cfg Wakeup threshold and duration. Pass NULL to skip setup.
 */
void lsm6dsv16x_init_accel_wakeup(
    const TagLsm6dsv16xDevice *device,
    const lsm6dsv16x_wakeup_mode_cfg_t *cfg,
    const lsm6dsv16x_motion_cfg_t *mot_cfg);

/**
 * MODE 3: accel + gyro HP, external ODR trigger on INT2, FIFO,
 * minimum filtering.  FIFO watermark and motion events on INT1.
 *
 * The driver calls the descriptor trigger callback with the divisor derived
 * from cfg->trig_odr. The application must not call that callback directly.
 *
 * If the descriptor has no trigger callback, this function returns without
 * touching the sensor. The sensor remains powered and configured after a
 * successful call.
 *
 * @param device Concrete device descriptor. Must not be NULL.
 * @param cfg Trigger ODR selection. Must not be NULL.
 * @param mot_cfg Motion detection parameters. Pass NULL to skip.
 */
void lsm6dsv16x_init_accel_gyro_triggered(
    const TagLsm6dsv16xDevice *device,
    const lsm6dsv16x_trig_mode_cfg_t *cfg,
    const lsm6dsv16x_motion_cfg_t *mot_cfg);

/**
 * Update wakeup / stationary-motion parameters at runtime.
 * All three accel axes are used for detection.
 *
 * @param device Concrete device descriptor. Must not be NULL.
 * @param cfg Wakeup threshold and duration. Must not be NULL.
 */
void lsm6dsv16x_set_motion_detection(const TagLsm6dsv16xDevice *device,
                                     const lsm6dsv16x_motion_cfg_t *cfg);

/**
 * Set accelerometer and gyroscope full-scale ranges.
 * Filter configuration is not affected.
 *
 * This may be called after an initializer to override the default +/-2 g
 * accelerometer and +/-2000 dps gyroscope ranges used by the mode setup.
 *
 * @param device Concrete device descriptor. Must not be NULL.
 * @param cfg Full-scale selection. Must not be NULL.
 */
void lsm6dsv16x_set_ranges(const TagLsm6dsv16xDevice *device,
                           const lsm6dsv16x_range_cfg_t *cfg);

/**
 * Set FIFO watermark.  INT1 asserts when unread words >= wtm.
 *
 * @param device Concrete device descriptor. Must not be NULL.
 * @param wtm 0-511 FIFO words (each word = 1 tag byte + 6 data bytes).
 */
void lsm6dsv16x_set_fifo_watermark(const TagLsm6dsv16xDevice *device,
                                   uint16_t wtm);

/**
 * Drain the FIFO and unpack accel+gyro pairs into samples[].
 * Non-sensor tags (temperature, timestamp, etc.) are consumed and discarded.
 *
 * @param[in]  device Concrete device descriptor. Must not be NULL.
 * @param[out] samples   Destination array.
 * @param[in]  max_pairs Capacity of samples[].
 * @return Number of complete pairs written.
 */
uint16_t lsm6dsv16x_read_fifo(const TagLsm6dsv16xDevice *device,
                              lsm6dsv16x_sample_t *samples,
                              uint16_t max_pairs);

/**
 * Query the parameters the driver will use for a given trigger ODR.
 * Informational; the driver programs the trigger callback automatically.
 * Useful for logging or verification during bringup.
 *
 * @param[in]  trig_odr  Desired ODR enum value.
 * @param[out] params    Filled on success.
 * @return 0 on success, -1 if trig_odr is not recognised.
 */
int lsm6dsv16x_get_trig_params(lsm6dsv16x_trig_odr_t     trig_odr,
                                lsm6dsv16x_trig_params_t *params);

/**
 * Accelerometer self-test (DS13726 Section 4.5 procedure).
 * Temporarily reconfigures to HP/120 Hz/+/-4 g; checks per-axis deflection
 * against limits of 50-1700 mg.  Leaves device in shutdown on return.
 * Calls the trigger callback with divider 0 before returning.
 * Re-initialise with the desired mode after checking the result.
 * Typical duration: ~200 ms.
 *
 * Diagnostic messages are emitted through debug_log_printf() when the
 * debug_log module is selected. They are no-ops otherwise.
 *
 * @return lsm6dsv16x_self_test_result_t (PASS = 0, negative on failure).
 */
lsm6dsv16x_self_test_result_t lsm6dsv16x_self_test_accel(
    const TagLsm6dsv16xDevice *device);

/**
 * Read a raw accelerometer sample from the output registers.
 * Polls STATUS_REG.XLDA.  Returns 0 if data was ready, -1 if not.
 * Intended for use in MODE 1 (no FIFO).
 *
 * @param[in] device Concrete device descriptor. Must not be NULL.
 * @param[out] x, y, z  Raw 16-bit two's-complement values.
 */
int lsm6dsv16x_read_accel(const TagLsm6dsv16xDevice *device,
                          int16_t *x, int16_t *y, int16_t *z);

/**
 * Read and return WAKE_UP_SRC (clears latched interrupt when LIR=1).
 * Returns 0 when the descriptor is invalid or the register read fails.
 *
 * Bit meanings (DS13726 Table 59):
 *   bit 5  SLEEP_STATE   device is in sleep (stationary)
 *   bit 4  WU_IA         wakeup event occurred
 *   bit 3  X_WU          wakeup on X axis
 *   bit 2  Y_WU          wakeup on Y axis
 *   bit 1  Z_WU          wakeup on Z axis
 *   bit 0  SLEEP_CHANGE  sleep/wakeup state just changed
 */
uint8_t lsm6dsv16x_read_wakeup_src(const TagLsm6dsv16xDevice *device);

/**
 * Read FIFO status; return number of unread words (0-511).
 *
 * @param[in] device Concrete device descriptor. Must not be NULL.
 * @param[out] wtm_flag  Set to 1 if watermark reached; 0 otherwise.  Pass NULL if unused.
 * @param[out] ovr_flag  Set to 1 if FIFO overrun occurred; 0 otherwise.  Pass NULL if unused.
 */
uint16_t lsm6dsv16x_read_fifo_status(const TagLsm6dsv16xDevice *device,
                                     uint8_t *wtm_flag, uint8_t *ovr_flag);

#endif /* LSM6DSV16X_H */
