/**
 * @file    lsm6dsv16x.c
 * @brief   Descriptor-backed driver for the STMicroelectronics LSM6DSV16X IMU.
 *
 * References
 * ----------
 *  [DS]  LSM6DSV16X Datasheet, DS13726, rev 5 (ST)
 *  [AN]  AN5763 – LSM6DSV16X application note (ST)
 *  [DT]  DT0155 – Synchronising multiple sensors using ODR-triggered mode (ST)
 *
 * Filtering policy
 * ----------------
 * Nyquist-minimum filtering is applied in all modes:
 *
 *   Accelerometer
 *     LPF1 is always active with its reset-default cutoff of ODR/2.  This is
 *     the inherent ADC output filter and cannot be disabled.  It satisfies the
 *     Nyquist criterion (cutoff = ODR/2) with no additional attenuation.
 *     LPF2 is disabled in all modes (LPF2_XL_EN = 0 in CTRL8).
 *     CTRL8 is therefore written as:  (FS_XL[1:0] only, bits [7:5] = 000,
 *     bit 3 = 0) = (uint8_t)fs_code.
 *
 *   Gyroscope (MODE 3 only)
 *     The optional LPF1 is disabled (LPF1_G_EN = 0, CTRL7 = 0x00).
 *     Only the ADC's inherent anti-alias response remains active.
 *
 * Driver ownership
 * ----------------
 * This module owns the LSM6DSV16X register programming sequence.  The concrete
 * tag/family owns board wiring through TagLsm6dsv16xDevice: register bus,
 * chip select, bus power policy, and the optional ODR-trigger callback.
 *
 * Active-mode initializers leave bus power enabled because later status/read
 * calls are expected.  Shutdown and self-test finish by powering the device
 * down.  Direct reads and setters bracket their register transactions with
 * tagBusBegin()/tagBusEnd().
 *
 * ODR-Triggered Mode Reference Table
 * ----------------------------------
 * f_out = 2S x (1024 / D), where D is the MCU timer divisor and S is the
 * value written to ODR_TRIG_N_ODR.  ST defines that value with 2-sample
 * resolution, so S = 25 requests 50 samples per reference period.  The
 * standard rates all use S = 25, which gives exact output rates from integer
 * divisors of the 1024 Hz low-speed timer.  ODR_TRIG_N_ODR is 9 bits: low
 * 8 bits in ODR_TRIG_CFG and bit 8 in COUNTER_BDR_REG1[5].  For S = 25, bit
 * 8 is zero.
 *
 *  Target  MCU div D  f_mcu     S   f_out   CTRL1/2
 *    50 Hz   D=1024    1.000 Hz 25   50 Hz   0x35
 *   100 Hz   D= 512    2.000 Hz 25  100 Hz   0x36
 *   200 Hz   D= 256    4.000 Hz 25  200 Hz   0x37
 *   400 Hz   D= 128    8.000 Hz 25  400 Hz   0x38
 *   800 Hz   D=  64   16.000 Hz 25  800 Hz   0x39
 *  1600 Hz   D=  32   32.000 Hz 25 1600 Hz   0x3A
 */

#include <stdbool.h>
#include <stddef.h>

#include "debug_log.h"
#include "hal.h"
#include "lsm6dsv16x_regs.h"
#include "timekeeping.h"


/* =========================================================================
 * Internal helpers
 * ====================================================================== */

static bool valid_device(const TagLsm6dsv16xDevice *device)
{
    return device != NULL && device->registers != NULL;
}

static void trigger_set(const TagLsm6dsv16xDevice *device, unsigned int divider)
{
    if (device != NULL && device->set_trigger != NULL) {
        device->set_trigger(device->trigger_context, divider);
    }
}

static void device_power_on(const TagLsm6dsv16xDevice *device)
{
    tagBusPowerOn(&device->registers->bus);
}

static void device_power_off(const TagLsm6dsv16xDevice *device)
{
    tagBusPowerOff(&device->registers->bus);
}

static void device_begin(const TagLsm6dsv16xDevice *device)
{
    tagBusBegin(&device->registers->bus);
}

static void device_end(const TagLsm6dsv16xDevice *device)
{
    tagBusEnd(&device->registers->bus);
}

static int reg_write(const TagLsm6dsv16xDevice *device, uint8_t reg,
                     uint8_t val)
{
    return tagRegisterWrite(device->registers, reg, &val, 1);
}

static int reg_read(const TagLsm6dsv16xDevice *device, uint8_t reg,
                    uint8_t *val)
{
    return tagRegisterRead(device->registers, reg, val, 1);
}

static int reg_read_block(const TagLsm6dsv16xDevice *device, uint8_t reg,
                          uint8_t *buf, uint32_t len)
{
    return tagRegisterRead(device->registers, reg, buf, len);
}

/* Read-modify-write: clear bits in mask, then OR in val. */
static int reg_update(const TagLsm6dsv16xDevice *device, uint8_t reg,
                      uint8_t mask, uint8_t val)
{
    uint8_t tmp = 0;
    if (reg_read(device, reg, &tmp) != 0) {
        return -1;
    }
    tmp = (uint8_t)((tmp & ~mask) | (val & mask));
    return reg_write(device, reg, tmp);
}

/* Software reset; SW_RESET bit auto-clears.  Wait 5 ms for completion. */
static void device_reset(const TagLsm6dsv16xDevice *device)
{
    reg_write(device, LSM6DSV16X_CTRL3, LSM6DSV16X_CTRL3_SW_RESET);
    stopMilliseconds(5);
}

/* BDU + IF_INC required by every mode. */
static void apply_common_defaults(const TagLsm6dsv16xDevice *device)
{
    reg_write(device, LSM6DSV16X_CTRL3,
              LSM6DSV16X_CTRL3_IF_INC | LSM6DSV16X_CTRL3_BDU);
}

/* =========================================================================
 * CTRL1 / CTRL2 register builders
 *
 * CTRL1 (0x10):  bits [6:4] = OP_MODE_XL,  bits [3:0] = ODR_XL
 * CTRL2 (0x11):  bits [6:4] = OP_MODE_G,   bits [3:0] = ODR_G
 * ====================================================================== */
static uint8_t build_ctrl1(lsm6dsv16x_xl_op_mode_t op, lsm6dsv16x_xl_odr_t odr)
{
    return (uint8_t)(((uint8_t)op << 4) | ((uint8_t)odr & 0x0FU));
}

static uint8_t build_ctrl2(lsm6dsv16x_g_op_mode_t op, lsm6dsv16x_g_odr_t odr)
{
    return (uint8_t)(((uint8_t)op << 4) | ((uint8_t)odr & 0x0FU));
}

/* =========================================================================
 * CTRL8 register builder – Nyquist-minimum accelerometer filter
 *
 * CTRL8 (0x17):
 *   bits [7:5] = HP_LPF2_XL_BW  – irrelevant when LPF2_XL_EN = 0
 *   bit  [3]   = LPF2_XL_EN     – 0: LPF2 disabled (minimum filtering)
 *   bits [1:0] = FS_XL
 *
 * With LPF2 disabled, LPF1 runs at its reset-default ODR/2 cutoff, which
 * is the minimum anti-alias filter consistent with the Nyquist criterion.
 * ====================================================================== */
static uint8_t build_ctrl8_min_filter(lsm6dsv16x_xl_fs_t fs)
{
    /* LPF2_XL_EN = 0, BW field = 000 (don't care), FS_XL in bits [1:0] */
    return (uint8_t)((uint8_t)fs & 0x03U);
}

/* =========================================================================
 * Wakeup / motion detection helper (shared by MODE 2 init and standalone setter)
 * ====================================================================== */
static void apply_motion_cfg(const TagLsm6dsv16xDevice *device,
                             const lsm6dsv16x_motion_cfg_t *cfg)
{
    uint8_t wake_up_ths;
    uint8_t wake_up_dur;

    if (cfg == NULL) { return; }

    /*
     * WAKE_UP_THS (0x5B):
     *   bits [5:0] = WK_THS
     *   bit  [6]   = SLEEP_ON = 1  (enables both wakeup and inactivity detection)
     */
    wake_up_ths = (uint8_t)(((uint8_t)cfg->threshold & 0x3FU) | 0x40U);
    reg_write(device, LSM6DSV16X_WAKE_UP_THS, wake_up_ths);

    /*
     * WAKE_UP_DUR (0x5C):
     *   bits [7:4] = SLEEP_DUR
     *   bits [1:0] = WAKE_DUR
     */
    wake_up_dur = (uint8_t)((((uint8_t)cfg->sleep_dur & 0x0FU) << 4) |
                              ((uint8_t)cfg->wakeup_dur & 0x03U));
    reg_write(device, LSM6DSV16X_WAKE_UP_DUR, wake_up_dur);
}

/* =========================================================================
 * FIFO stream configuration helper
 * ====================================================================== */
static void configure_fifo_stream(const TagLsm6dsv16xDevice *device,
                                  lsm6dsv16x_bdr_t bdr)
{
    uint8_t fifo_ctrl3;

    /* Reset FIFO to bypass first to discard any stale content */
    reg_write(device, LSM6DSV16X_FIFO_CTRL4, LSM6DSV16X_FIFO_MODE_BYPASS);

    /*
     * FIFO_CTRL3 (0x09):
     *   bits [7:4] = BDR_GY  (gyroscope batch data rate)
     *   bits [3:0] = BDR_XL  (accelerometer batch data rate)
     * Both sensors are batched at the same rate.
     */
    fifo_ctrl3 = (uint8_t)((((uint8_t)bdr & 0x0FU) << 4) |
                              ((uint8_t)bdr & 0x0FU));
    reg_write(device, LSM6DSV16X_FIFO_CTRL3, fifo_ctrl3);

    /* Continuous / stream mode */
    reg_write(device, LSM6DSV16X_FIFO_CTRL4, LSM6DSV16X_FIFO_MODE_STREAM);
}

static void set_fifo_watermark_registers(const TagLsm6dsv16xDevice *device,
                                         uint16_t wtm)
{
    if (wtm > 511U) { wtm = 511U; }
    reg_write(device, LSM6DSV16X_FIFO_CTRL1, (uint8_t)(wtm & 0xFFU));
    reg_update(device, LSM6DSV16X_FIFO_CTRL2, 0x01U,
               (uint8_t)((wtm >> 8U) & 0x01U));
}

static void shutdown_registers(const TagLsm6dsv16xDevice *device)
{
    reg_write(device, LSM6DSV16X_CTRL1,      0x00U);
    reg_write(device, LSM6DSV16X_CTRL2,      0x00U);
    reg_write(device, LSM6DSV16X_CTRL3,      0x01U);  // software reset
    /*
    reg_write(device, LSM6DSV16X_CTRL9,      0x00U);
    reg_write(device, LSM6DSV16X_CTRL4,      0x00U);
    reg_write(device, LSM6DSV16X_FIFO_CTRL4, LSM6DSV16X_FIFO_MODE_BYPASS);
    reg_write(device, LSM6DSV16X_FIFO_CTRL1, 0x00U);
    reg_write(device, LSM6DSV16X_FIFO_CTRL2, 0x00U);
    reg_write(device, LSM6DSV16X_FIFO_CTRL3, 0x00U);
    reg_write(device, LSM6DSV16X_ODR_TRIG_CFG, 0x00U);
    reg_write(device, LSM6DSV16X_COUNTER_BDR_REG1, 0x00U);
    reg_write(device, LSM6DSV16X_COUNTER_BDR_REG2, 0x00U);
    reg_write(device, LSM6DSV16X_INT1_CTRL,  0x00U);
    reg_write(device, LSM6DSV16X_INT2_CTRL,  0x00U);
    reg_write(device, LSM6DSV16X_MD1_CFG,    0x00U);
    reg_write(device, LSM6DSV16X_MD2_CFG,    0x00U);
    reg_write(device, LSM6DSV16X_TAP_CFG0,   0x00U);
    reg_write(device, LSM6DSV16X_TAP_CFG2,   0x00U);
    reg_write(device, LSM6DSV16X_WAKE_UP_THS, 0x00U);
    reg_write(device, LSM6DSV16X_WAKE_UP_DUR, 0x00U);
    */
}

/* =========================================================================
 * ODR-triggered mode lookup table
 *
 * The MCU timer runs from 1024 Hz.  ODR_TRIG_N_ODR has 2-sample resolution,
 * so S = 25 gives 50 samples per reference period and exact
 * 50/100/200/400/800/1600 Hz rates with the divisors below.
 * ====================================================================== */
static const odr_trig_entry_t odr_trig_table[] = {
    /*   hz    odr_code                  n_odr                         bdr                    mcu_div_d */
    {   50, LSM6DSV16X_G_ODR_60Hz,  LSM6DSV16X_ODR_TRIG_N_ODR_S25, LSM6DSV16X_BDR_60Hz,   1024 },
    {  100, LSM6DSV16X_G_ODR_120Hz, LSM6DSV16X_ODR_TRIG_N_ODR_S25, LSM6DSV16X_BDR_120Hz,   512 },
    {  200, LSM6DSV16X_G_ODR_240Hz, LSM6DSV16X_ODR_TRIG_N_ODR_S25, LSM6DSV16X_BDR_240Hz,   256 },
    {  400, LSM6DSV16X_G_ODR_480Hz, LSM6DSV16X_ODR_TRIG_N_ODR_S25, LSM6DSV16X_BDR_480Hz,   128 },
    {  800, LSM6DSV16X_G_ODR_960Hz, LSM6DSV16X_ODR_TRIG_N_ODR_S25, LSM6DSV16X_BDR_960Hz,    64 },
    { 1600, LSM6DSV16X_G_ODR_1920Hz, LSM6DSV16X_ODR_TRIG_N_ODR_S25, LSM6DSV16X_BDR_1920Hz,  32 },
};
static const size_t odr_trig_table_len =
    sizeof(odr_trig_table) / sizeof(odr_trig_table[0]);

static const odr_trig_entry_t *find_trig_entry(lsm6dsv16x_trig_odr_t trig_odr)
{
    size_t i;
    for (i = 0; i < odr_trig_table_len; i++) {
        if ((uint16_t)trig_odr == odr_trig_table[i].hz) {
            return &odr_trig_table[i];
        }
    }
    return &odr_trig_table[0];   /* safe fallback: lowest supported rate */
}

/* =========================================================================
 * Public API – mode initialisation
 * ====================================================================== */

int lsm6dsv16x_verify_id(const TagLsm6dsv16xDevice *device)
{
    uint8_t whoami = 0;
    if (!valid_device(device)) {
        return -1;
    }

    device_power_on(device);
    device_begin(device);
    reg_read(device, LSM6DSV16X_WHO_AM_I, &whoami);
    device_end(device);
    return (whoami == LSM6DSV16X_WHOAMI_VALUE) ? 0 : -1;
}

/* -------------------------------------------------------------------------
 * MODE 0 – Shutdown
 *
 * Powers down both sensors, disables the FIFO, clears all interrupt routing,
 * and disables the external ODR trigger callback by passing divider 0.
 * ---------------------------------------------------------------------- */
void lsm6dsv16x_init_shutdown(const TagLsm6dsv16xDevice *device)
{
    if (!valid_device(device)) {
        return;
    }

    /* Disable external trigger before touching sensor registers */
    trigger_set(device, 0U);

    device_power_on(device);
    device_begin(device);
    shutdown_registers(device);
    device_end(device);
    device_power_off(device);
}

/* -------------------------------------------------------------------------
 * MODE 1 – Accelerometer only, high-performance, internal ODR
 *
 * Filter path: ADC → LPF1 (ODR/2, always active) → output register.
 * LPF2 disabled.  Data-ready on INT1.
 * Disables the trigger callback with divider 0; trigger is only valid in
 * MODE 3.
 *
 * Sequence ([AN] Section 3.7):
 *  1. Software reset.
 *  2. BDU + IF_INC.
 *  3. Write CTRL8: LPF2 disabled, FS_XL = ±2 g (default; change via
 *     lsm6dsv16x_set_ranges() after init).
 *  4. Write CTRL1: HP mode, requested ODR.
 *  5. Route DRDY_XL to INT1.
 *  6. Disable external trigger.
 * ---------------------------------------------------------------------- */
void lsm6dsv16x_init_accel_only(const TagLsm6dsv16xDevice *device,
                                const lsm6dsv16x_accel_cfg_t *cfg)
{
    if (!valid_device(device) || cfg == NULL) { return; }

    trigger_set(device, 0U);
    device_power_on(device);
    device_begin(device);

    device_reset(device);
    apply_common_defaults(device);

    /* CTRL8: LPF2 disabled, FS = ±2 g */
    reg_write(device, LSM6DSV16X_CTRL8,
              build_ctrl8_min_filter(LSM6DSV16X_XL_FS_2G));

    /* CTRL1: HP mode, requested ODR; gyroscope stays in power-down */
    reg_write(device, LSM6DSV16X_CTRL1,
              build_ctrl1(LSM6DSV16X_XL_OP_HP, cfg->odr));

    /* Route accel data-ready to INT1 */
    reg_write(device, LSM6DSV16X_INT1_CTRL, LSM6DSV16X_INT1_DRDY_XL);

    device_end(device);
}

/* -------------------------------------------------------------------------
 * MODE 2 – Accelerometer wakeup, low-power, slope filter
 *
 * Filter path: ADC → LPF1 (ODR/2) → slope filter → wakeup comparator.
 * LPF2 disabled.  Wakeup and stationary/motion events on INT1.
 * Disables the trigger callback with divider 0.
 *
 * Sequence ([AN] Section 5.2):
 *  1. Software reset.
 *  2. BDU + IF_INC.
 *  3. CTRL8: LPF2 disabled, FS = ±2 g.
 *  4. CTRL1: LP mode, requested ODR.
 *  5. TAP_CFG0: LIR=1, SLOPE_FDS=0 (slope filter, not HP, feeds wakeup).
 *  6. WAKE_UP_THS / WAKE_UP_DUR: threshold and durations.
 *  7. TAP_CFG2: master interrupt enable.
 *  8. MD1_CFG: wakeup → INT1.
 *  9. Disable external trigger.
 * ---------------------------------------------------------------------- */
void lsm6dsv16x_init_accel_wakeup(
    const TagLsm6dsv16xDevice *device,
    const lsm6dsv16x_wakeup_mode_cfg_t *cfg,
    const lsm6dsv16x_motion_cfg_t *mot_cfg)
{
    if (!valid_device(device) || cfg == NULL) { return; }

    trigger_set(device, 0U);
    device_power_on(device);
    device_begin(device);

    device_reset(device);
    apply_common_defaults(device);

    reg_write(device, LSM6DSV16X_CTRL8,
              build_ctrl8_min_filter(LSM6DSV16X_XL_FS_2G));
    reg_write(device, LSM6DSV16X_CTRL1,
              build_ctrl1(LSM6DSV16X_XL_OP_LP, cfg->odr));

    /* Slope filter selected (SLOPE_FDS=0 is reset default); latch interrupt */
    reg_write(device, LSM6DSV16X_TAP_CFG0, LSM6DSV16X_TAP_CFG0_LIR);

    apply_motion_cfg(device, mot_cfg);

    reg_write(device, LSM6DSV16X_TAP_CFG2, LSM6DSV16X_TAP_CFG2_INTR_EN);
    reg_write(device, LSM6DSV16X_MD1_CFG,  LSM6DSV16X_MD1_INT1_WU);

    device_end(device);
}

/* -------------------------------------------------------------------------
 * MODE 3 – Accel + Gyro, external ODR trigger, FIFO, minimum filtering
 *
 * Filter paths:
 *   Accelerometer: ADC → LPF1 (ODR/2, inherent) → FIFO.  LPF2 disabled.
 *   Gyroscope:     ADC → FIFO.  LPF1_G_EN = 0 (optional LPF1 disabled).
 *
 * FIFO watermark and stationary/motion events on INT1.
 * The driver calls the trigger callback with D after configuring the sensor
 * so the trigger signal is stable before the sensor starts counting edges.
 *
 * Sequence ([AN] Section 3.3, Listing 2):
 *  1.  Software reset.
 *  2.  BDU + IF_INC.
 *  3.  Leave PA8 analog while INT2 is still an IMU interrupt output.
 *  4.  Hold both sensors in power-down; wait at least 500 us ([AN]
 *      requirement before writing ODR-triggered configuration registers).
 *      The tag timekeeping API rounds this up to stopMilliseconds(1).
 *  5.  CTRL8: LPF2 disabled, configured accelerometer full-scale range.
 *  6.  CTRL6: configured gyro full-scale range, LPF1_G_BW = 000
 *      (LPF1 disabled via CTRL7).
 *  7.  CTRL7 = 0x00: LPF1_G_EN = 0 (gyro LPF1 off – minimum filtering).
 *  8.  Write ODR_TRIG_N_ODR: low byte in ODR_TRIG_CFG, bit 8 in
 *      COUNTER_BDR_REG1[5].
 *  9.  CTRL9: ODR_TRIG_EN = 1.
 * 10.  CTRL1 / CTRL2: ODR-triggered mode with ODRsel still zero.
 * 11.  CTRL4: INT2_IN_LH = 1 (rising edge).
 * 12.  FIFO: set default watermark, enable stream mode at matching BDR.
 * 13.  Motion detection (if mot_cfg != NULL).
 * 14.  Master interrupt enable.
 * 15.  Route FIFO watermark + sleep-change to INT1.
 * 16.  Call the trigger callback with D to start the external clock.
 * 17.  CTRL1 / CTRL2: write ODRsel to start sampling.
 * ---------------------------------------------------------------------- */
void lsm6dsv16x_init_accel_gyro_triggered(
    const TagLsm6dsv16xDevice *device,
    const lsm6dsv16x_trig_mode_cfg_t *cfg,
    const lsm6dsv16x_motion_cfg_t *mot_cfg)
{
    const odr_trig_entry_t *entry;

    if (!valid_device(device) || cfg == NULL || device->set_trigger == NULL) {
        return;
    }

    entry = find_trig_entry(cfg->trig_odr);

    trigger_set(device, 0U);
    device_power_on(device);
    device_begin(device);

    /* 1–2 */
    device_reset(device);
    apply_common_defaults(device);

    /*
     * 3. PA8 remains analog while INT2 is still an IMU interrupt output. The
     *    trigger callback is called only after the device is configured for
     *    ODR-triggered mode, so INT1 can remain push-pull for the wakeup line.
     */

    /* 4. Hold in power-down per [AN]; 500 us minimum rounded to 1 ms. */
    reg_write(device, LSM6DSV16X_CTRL1, 0x00U);
    reg_write(device, LSM6DSV16X_CTRL2, 0x00U);
    stopMilliseconds(1);

    /* 5. Accel: LPF2 disabled, configured full-scale range */
    reg_write(device, LSM6DSV16X_CTRL8,
              build_ctrl8_min_filter(cfg->xl_fs));

    /* 6. Gyro: configured full-scale range; LPF1_G_BW irrelevant */
    reg_write(device, LSM6DSV16X_CTRL6,
              (uint8_t)cfg->g_fs & 0x0FU);

    /* 7. Gyro LPF1 disabled – Nyquist-minimum filtering */
    reg_write(device, LSM6DSV16X_CTRL7, 0x00U);

    /*
     * 8. ODR_TRIG_N_ODR is the multiplier S.  Bits [7:0] are in
     *    ODR_TRIG_CFG; bit 8 is COUNTER_BDR_REG1[5].
     */
    reg_write(device, LSM6DSV16X_ODR_TRIG_CFG,
              (uint8_t)(entry->n_odr & 0xFFU));
    reg_update(device, LSM6DSV16X_COUNTER_BDR_REG1, 0x20U,
               (uint8_t)(((entry->n_odr >> 8U) & 0x01U) << 5));

    /* 9. Enable ODR-triggered mode */
    reg_write(device, LSM6DSV16X_CTRL9, LSM6DSV16X_CTRL9_ODR_TRIG_EN);

    /* 10. Both sensors in ODR-triggered mode, ODRsel still zero. */
    reg_write(device, LSM6DSV16X_CTRL1,
              build_ctrl1(LSM6DSV16X_XL_OP_TRIGGERED,
                          LSM6DSV16X_XL_ODR_POWER_DOWN));
    reg_write(device, LSM6DSV16X_CTRL2,
              build_ctrl2(LSM6DSV16X_G_OP_TRIGGERED,
                          LSM6DSV16X_G_ODR_POWER_DOWN));

    /* 11. Rising-edge trigger on INT2 */
    reg_write(device, LSM6DSV16X_CTRL4, LSM6DSV16X_CTRL4_INT2_IN_LH);

    /* 12. FIFO */
    set_fifo_watermark_registers(device, 32U);
    configure_fifo_stream(device, entry->bdr);

    /* 13. Motion detection */
    if (mot_cfg != NULL) {
        reg_write(device, LSM6DSV16X_TAP_CFG0, LSM6DSV16X_TAP_CFG0_LIR);
        apply_motion_cfg(device, mot_cfg);
    }

    /* 14. Master interrupt enable */
    reg_write(device, LSM6DSV16X_TAP_CFG2, LSM6DSV16X_TAP_CFG2_INTR_EN);

    /* 15. FIFO watermark only → INT1 */
    reg_write(device, LSM6DSV16X_INT1_CTRL, LSM6DSV16X_INT1_FIFO_TH);
    reg_write(device, LSM6DSV16X_MD1_CFG,   0x00U);

    /* 16. Start the external trigger.  The sensor will begin locking to INT2
     *     edges within one trigger period after this call. */
    trigger_set(device, (unsigned int)entry->mcu_div_d);

    /* 17. Write ODRsel after the external reference signal has started. */
    reg_write(device, LSM6DSV16X_CTRL1,
              build_ctrl1(LSM6DSV16X_XL_OP_TRIGGERED,
                          (lsm6dsv16x_xl_odr_t)entry->odr_code));
    reg_write(device, LSM6DSV16X_CTRL2,
              build_ctrl2(LSM6DSV16X_G_OP_TRIGGERED,
                          (lsm6dsv16x_g_odr_t)entry->odr_code));

    device_end(device);
}

/* =========================================================================
 * Standalone setters
 * ====================================================================== */

void lsm6dsv16x_set_motion_detection(const TagLsm6dsv16xDevice *device,
                                     const lsm6dsv16x_motion_cfg_t *cfg)
{
    if (!valid_device(device) || cfg == NULL) { return; }

    device_power_on(device);
    device_begin(device);
    apply_motion_cfg(device, cfg);
    device_end(device);
}

/**
 * Set accelerometer and gyroscope full-scale ranges.
 *
 * CTRL8 [1:0] = FS_XL; CTRL6 [3:0] = FS_G.
 * Only the FS bits are changed; filter configuration is not disturbed.
 */
void lsm6dsv16x_set_ranges(const TagLsm6dsv16xDevice *device,
                           const lsm6dsv16x_range_cfg_t *cfg)
{
    if (!valid_device(device) || cfg == NULL) { return; }

    device_power_on(device);
    device_begin(device);
    reg_update(device, LSM6DSV16X_CTRL8, 0x03U,
               (uint8_t)cfg->xl_fs & 0x03U);
    reg_update(device, LSM6DSV16X_CTRL6, 0x0FU,
               (uint8_t)cfg->g_fs  & 0x0FU);
    device_end(device);
}

/**
 * Set FIFO watermark threshold.
 *
 * FIFO_CTRL1 [7:0] = WTM[7:0]
 * FIFO_CTRL2 [0]   = WTM[8]
 */
void lsm6dsv16x_set_fifo_watermark(const TagLsm6dsv16xDevice *device,
                                   uint16_t wtm)
{
    if (!valid_device(device)) { return; }

    device_power_on(device);
    device_begin(device);
    set_fifo_watermark_registers(device, wtm);
    device_end(device);
}

/* =========================================================================
 * FIFO read and unpack
 * ====================================================================== */
/**
 * Each FIFO word is 7 bytes: tag byte + 6 data bytes. Tag byte bits [7:3]
 * identify the sensor and bits [1:0] carry the rotating time-slot counter.
 * Tag 0x01 = gyro, 0x02 = accel; all other tags are consumed and discarded.
 *
 * Gyro and accel words are paired by the low two counter bits, not by arrival
 * order. This tolerates either sensor arriving first. A counter change closes
 * the previous slot; if it was incomplete or otherwise inconsistent, a zeroed
 * placeholder sample is emitted so the returned stream preserves timing.
 */
#define LSM6DSV16X_FIFO_WORD_BYTES 7U
#define LSM6DSV16X_FIFO_TRACE_WORDS 32U
#define LSM6DSV16X_FIFO_TRACE_LIMIT 4U


static uint8_t fifo_read_buffer[MAX_LSM6DSV16X_FIFO_READ * LSM6DSV16X_FIFO_WORD_BYTES];

typedef struct {
    int16_t gx, gy, gz;
    int16_t ax, ay, az;
    uint8_t have_gyro;
    uint8_t have_accel;
    uint8_t error;
} fifo_pending_sample_t;

typedef struct {
    fifo_pending_sample_t pending;
    uint8_t  current_cnt;
    uint8_t  have_current_cnt;
    uint8_t  current_slot_closed;
    uint16_t pairs_out;
    uint16_t zeroed_slots;
    uint8_t  trace_tags[LSM6DSV16X_FIFO_TRACE_WORDS];
    uint8_t  trace_count;
} fifo_parse_context_t;

static uint8_t fifo_trace_logs;

static void fifo_reset_pending(fifo_pending_sample_t *pending)
{
    pending->have_gyro = 0U;
    pending->have_accel = 0U;
    pending->error = 0U;
}

static void fifo_write_zero_sample(lsm6dsv16x_sample_t *sample)
{
    sample->gyro_x = 0;
    sample->gyro_y = 0;
    sample->gyro_z = 0;
    sample->accel_x = 0;
    sample->accel_y = 0;
    sample->accel_z = 0;
}

static void fifo_write_pending_sample(const fifo_pending_sample_t *pending,
                                      lsm6dsv16x_sample_t *sample)
{
    sample->gyro_x = pending->gx;
    sample->gyro_y = pending->gy;
    sample->gyro_z = pending->gz;
    sample->accel_x = pending->ax;
    sample->accel_y = pending->ay;
    sample->accel_z = pending->az;
}

static uint8_t fifo_emit_pending_or_zero(const fifo_pending_sample_t *pending,
                                         lsm6dsv16x_sample_t *sample)
{
    if (pending->have_gyro && pending->have_accel && !pending->error) {
        fifo_write_pending_sample(pending, sample);
        return 0U;
    }

    fifo_write_zero_sample(sample);
    return 1U;
}

static void fifo_parse_context_init(fifo_parse_context_t *ctx)
{
    fifo_reset_pending(&ctx->pending);
    ctx->current_cnt = 0U;
    ctx->have_current_cnt = 0U;
    ctx->current_slot_closed = 0U;
    ctx->pairs_out = 0U;
    ctx->zeroed_slots = 0U;
    ctx->trace_count = 0U;
}

static bool fifo_parse_word(fifo_parse_context_t *ctx,
                            lsm6dsv16x_sample_t *samples,
                            uint16_t max_pairs,
                            const uint8_t *word)
{
    uint8_t tag;
    uint8_t cnt;
    int16_t x;
    int16_t y;
    int16_t z;

    if (ctx->trace_count < LSM6DSV16X_FIFO_TRACE_WORDS) {
        ctx->trace_tags[ctx->trace_count++] = word[0];
    }

    tag = (uint8_t)(word[0] >> 3U);
    cnt = (uint8_t)(word[0] & 0x03U);
    if (tag == LSM6DSV16X_FIFO_TAG_GYRO_NC ||
        tag == LSM6DSV16X_FIFO_TAG_ACCEL_NC) {
        if (ctx->have_current_cnt == 0U) {
            ctx->current_cnt = cnt;
            ctx->have_current_cnt = 1U;
        } else if (cnt != ctx->current_cnt) {
            if (ctx->pending.have_gyro || ctx->pending.have_accel ||
                ctx->pending.error) {
                ctx->zeroed_slots +=
                    fifo_emit_pending_or_zero(&ctx->pending,
                                              &samples[ctx->pairs_out]);
                ctx->pairs_out++;
                fifo_reset_pending(&ctx->pending);
                if (ctx->pairs_out >= max_pairs) {
                    return true;
                }
            }

            ctx->current_cnt = cnt;
            ctx->current_slot_closed = 0U;
        }

        x = (int16_t)((uint16_t)word[2] << 8U | (uint16_t)word[1]);
        y = (int16_t)((uint16_t)word[4] << 8U | (uint16_t)word[3]);
        z = (int16_t)((uint16_t)word[6] << 8U | (uint16_t)word[5]);

        if (ctx->current_slot_closed) {
            ctx->pending.error = 1U;
        }

        if (tag == LSM6DSV16X_FIFO_TAG_GYRO_NC) {
            if (ctx->pending.have_gyro) {
                ctx->pending.error = 1U;
            }
            ctx->pending.gx = x;
            ctx->pending.gy = y;
            ctx->pending.gz = z;
            ctx->pending.have_gyro = 1U;

        } else {
            if (ctx->pending.have_accel) {
                ctx->pending.error = 1U;
            }
            ctx->pending.ax = x;
            ctx->pending.ay = y;
            ctx->pending.az = z;
            ctx->pending.have_accel = 1U;
        }

        if (ctx->pending.have_gyro && ctx->pending.have_accel) {
            ctx->zeroed_slots +=
                fifo_emit_pending_or_zero(&ctx->pending,
                                          &samples[ctx->pairs_out]);
            ctx->pairs_out++;
            fifo_reset_pending(&ctx->pending);
            ctx->current_slot_closed = 1U;
        }
    }

    return ctx->pairs_out >= max_pairs;
}

static void fifo_parse_finalize(fifo_parse_context_t *ctx,
                                lsm6dsv16x_sample_t *samples,
                                uint16_t max_pairs)
{
    if ((ctx->pairs_out < max_pairs) &&
        (ctx->pending.have_gyro || ctx->pending.have_accel ||
         ctx->pending.error)) {
        ctx->zeroed_slots +=
            fifo_emit_pending_or_zero(&ctx->pending, &samples[ctx->pairs_out]);
        ctx->pairs_out++;
    }
}

static void fifo_log_parse_trace(const fifo_parse_context_t *ctx)
{
    if (ctx->zeroed_slots > 0U) {
        debug_log_printf("LSM6DSV16X FIFO: zeroed %u slot(s)\r\n",
                         (unsigned)ctx->zeroed_slots);
        if (fifo_trace_logs < LSM6DSV16X_FIFO_TRACE_LIMIT) {
            fifo_trace_logs++;
            debug_log_printf(
                "LSM6DSV16X FIFO tags[%u]: %x %x %x %x %x %x %x %x\r\n",
                (unsigned)ctx->trace_count,
                ctx->trace_count > 0U ? ctx->trace_tags[0] : 0U,
                ctx->trace_count > 1U ? ctx->trace_tags[1] : 0U,
                ctx->trace_count > 2U ? ctx->trace_tags[2] : 0U,
                ctx->trace_count > 3U ? ctx->trace_tags[3] : 0U,
                ctx->trace_count > 4U ? ctx->trace_tags[4] : 0U,
                ctx->trace_count > 5U ? ctx->trace_tags[5] : 0U,
                ctx->trace_count > 6U ? ctx->trace_tags[6] : 0U,
                ctx->trace_count > 7U ? ctx->trace_tags[7] : 0U);
            if (ctx->trace_count > 8U) {
                debug_log_printf(
                    "LSM6DSV16X FIFO tags: %x %x %x %x %x %x %x %x\r\n",
                    ctx->trace_count > 8U ? ctx->trace_tags[8] : 0U,
                    ctx->trace_count > 9U ? ctx->trace_tags[9] : 0U,
                    ctx->trace_count > 10U ? ctx->trace_tags[10] : 0U,
                    ctx->trace_count > 11U ? ctx->trace_tags[11] : 0U,
                    ctx->trace_count > 12U ? ctx->trace_tags[12] : 0U,
                    ctx->trace_count > 13U ? ctx->trace_tags[13] : 0U,
                    ctx->trace_count > 14U ? ctx->trace_tags[14] : 0U,
                    ctx->trace_count > 15U ? ctx->trace_tags[15] : 0U);
            }
        }
    }
}

uint16_t lsm6dsv16x_read_fifo(const TagLsm6dsv16xDevice *device,
                              lsm6dsv16x_sample_t *samples,
                              uint16_t max_pairs)
{
    fifo_parse_context_t ctx;
    uint8_t  st1 = 0;
    uint8_t  st2 = 0;
    uint16_t fifo_level;
    //uint8_t  word[LSM6DSV16X_FIFO_WORD_BYTES];

    if (!valid_device(device) || samples == NULL || max_pairs == 0U) {
        return 0U;
    }

    if (max_pairs*2 > MAX_LSM6DSV16X_FIFO_READ ) {
        max_pairs = MAX_LSM6DSV16X_FIFO_READ/2;
    }

    fifo_parse_context_init(&ctx);

    device_power_on(device);
    device_begin(device);
    reg_read(device, LSM6DSV16X_FIFO_STATUS1, &st1);
    reg_read(device, LSM6DSV16X_FIFO_STATUS2, &st2);
    fifo_level = (uint16_t)(((uint16_t)(st2 & 0x03U) << 8U) | (uint16_t)st1);

    if (fifo_level > max_pairs*2){
        fifo_level = max_pairs*2;
    }
    
    // read a block of data

    if ((fifo_level == 0) || reg_read_block(device, LSM6DSV16X_FIFO_DATA_OUT_TAG,
                                                    fifo_read_buffer, 
                                                    LSM6DSV16X_FIFO_WORD_BYTES*fifo_level)) {
        device_end(device);
        return 0U;
    }

    for (int i = 0; i < fifo_level; i++) {
        if (fifo_parse_word(&ctx, samples, max_pairs, &fifo_read_buffer[i * LSM6DSV16X_FIFO_WORD_BYTES])) {
            break;
        }
    }

    #if 0
    while ((fifo_level > 0U) && (ctx.pairs_out < max_pairs)) {

        if (reg_read_block(device, LSM6DSV16X_FIFO_DATA_OUT_TAG,
                            word, LSM6DSV16X_FIFO_WORD_BYTES) != 0) {
            break;
        }

        fifo_level--;

        if (fifo_parse_word(&ctx, samples, max_pairs, word)) {
            break;
        }
    }

    #endif

    // loop over the data 
    // fifo_parse_word.


    fifo_parse_finalize(&ctx, samples, fifo_level);
    fifo_log_parse_trace(&ctx);

    device_end(device);
    return ctx.pairs_out;
}

/* =========================================================================
 * ODR trigger parameter query  (informational; driver programs trigger internally)
 * ====================================================================== */
int lsm6dsv16x_get_trig_params(lsm6dsv16x_trig_odr_t     trig_odr,
                                lsm6dsv16x_trig_params_t *params)
{
    const odr_trig_entry_t *e = NULL;
    size_t i;

    if (params == NULL) { return -1; }

    for (i = 0; i < odr_trig_table_len; i++) {
        if ((uint16_t)trig_odr == odr_trig_table[i].hz) {
            e = &odr_trig_table[i];
            break;
        }
    }
    if (e == NULL) { return -1; }

    params->target_hz     = e->hz;
    params->mcu_div_d     = e->mcu_div_d;
    params->f_mcu_millihz = (uint32_t)1024U * 1000U / (uint32_t)e->mcu_div_d;
    params->multiplier_s   = e->n_odr;
    params->cbdr_reg1_msb  = (uint8_t)((e->n_odr >> 8U) & 0x01U);
    params->odr_trig_cfg   = (uint8_t)(e->n_odr & 0xFFU);
    params->ctrl_odr_code  = e->odr_code;
    params->fifo_bdr_code  = (uint8_t)e->bdr;

    return 0;
}

/* =========================================================================
 * Accelerometer self-test – MODE 1 / DS13726 Section 4.5 procedure
 *
 * Configuration: HP mode, 120 Hz ODR, ±4 g, LPF2 disabled.
 * Averages LSM6DSV16X_ST_N_AVG samples before and after enabling the
 * positive self-test signal.  Checks per-axis deltas against limits:
 *   50–1700 mg  →  410–13934 LSB at 0.122 mg/LSB sensitivity.
 *
 * Leaves the device shut down and disables the trigger callback before
 * returning.
 * ====================================================================== */

/* Read one sample, polling XLDA with a per-millisecond timeout. */
static int st_read_sample(const TagLsm6dsv16xDevice *device,
                          int32_t *ax, int32_t *ay, int32_t *az)
{
    uint32_t t;
    uint8_t  status = 0;
    uint8_t  raw[6];

    for (t = 0U; t < LSM6DSV16X_ST_DRDY_TIMEOUT_MS; t++) {
        if (reg_read(device, LSM6DSV16X_STATUS_REG, &status) == 0 &&
            (status & 0x01U) != 0U) {
            reg_read_block(device, LSM6DSV16X_OUTX_L_A, raw, 6U);
            *ax = (int32_t)(int16_t)((uint16_t)raw[1] << 8U | raw[0]);
            *ay = (int32_t)(int16_t)((uint16_t)raw[3] << 8U | raw[2]);
            *az = (int32_t)(int16_t)((uint16_t)raw[5] << 8U | raw[4]);
            return 0;
        }
        stopMilliseconds(1U);
    }
    return -1;
}

static void st_log_delta_failure(const char *axis, int32_t delta)
{
    debug_log_printf("LSM6DSV16X self-test: %s delta %d outside %d..%d\r\n",
                     axis,
                     (int)delta,
                     (int)LSM6DSV16X_ST_XL_MIN_LSB,
                     (int)LSM6DSV16X_ST_XL_MAX_LSB);
}

lsm6dsv16x_self_test_result_t lsm6dsv16x_self_test_accel(
    const TagLsm6dsv16xDevice *device)
{
    int32_t  sum_x, sum_y, sum_z;
    int32_t  nost_x, nost_y, nost_z;
    int32_t  st_x,   st_y,   st_z;
    int32_t  delta_x, delta_y, delta_z;
    int32_t  ax, ay, az;
    uint32_t s;
    int      rc;
    uint8_t  whoami = 0;
    uint8_t  ctrl10 = 0;
    lsm6dsv16x_self_test_result_t result = LSM6DSV16X_SELF_TEST_PASS;

    if (!valid_device(device)) {
        debug_log_printf("LSM6DSV16X self-test: invalid device descriptor\r\n");
        return LSM6DSV16X_SELF_TEST_FAIL_ID;
    }

    debug_log_printf("LSM6DSV16X self-test: start\r\n");
    trigger_set(device, 0U);
    device_power_on(device);
    device_begin(device);

    /* Step 1 – Identity check */
    if (reg_read(device, LSM6DSV16X_WHO_AM_I, &whoami) != 0 ||
        whoami != LSM6DSV16X_WHOAMI_VALUE) {
        debug_log_printf("LSM6DSV16X self-test: WHO_AM_I 0x%x expected 0x%x\r\n",
                         whoami,
                         LSM6DSV16X_WHOAMI_VALUE);
        result = LSM6DSV16X_SELF_TEST_FAIL_ID;
        goto cleanup;
    }

    /* Step 2 – Configure for self-test: HP, 120 Hz, ±4 g, LPF2 disabled */
    device_reset(device);
    apply_common_defaults(device);
    reg_write(device, LSM6DSV16X_CTRL8,
              build_ctrl8_min_filter(LSM6DSV16X_XL_FS_4G));
    reg_write(device, LSM6DSV16X_CTRL1,
              build_ctrl1(LSM6DSV16X_XL_OP_HP, LSM6DSV16X_XL_ODR_120Hz));

    /* Step 3 – Settle */
    stopMilliseconds(100U);

    /* Step 4 – Discard first sample (pipeline flush) */
    rc = st_read_sample(device, &ax, &ay, &az);
    if (rc != 0) {
        debug_log_printf("LSM6DSV16X self-test: timeout waiting for initial sample\r\n");
        result = LSM6DSV16X_SELF_TEST_FAIL_TO;
        goto cleanup;
    }

    /* Step 5 – Average without self-test (NOST) */
    sum_x = 0; sum_y = 0; sum_z = 0;
    for (s = 0U; s < LSM6DSV16X_ST_N_AVG; s++) {
        rc = st_read_sample(device, &ax, &ay, &az);
        if (rc != 0) {
            debug_log_printf("LSM6DSV16X self-test: timeout before ST sample %u\r\n",
                             (unsigned)s);
            result = LSM6DSV16X_SELF_TEST_FAIL_TO;
            goto cleanup;
        }
        sum_x += ax; sum_y += ay; sum_z += az;
    }
    nost_x = sum_x / (int32_t)LSM6DSV16X_ST_N_AVG;
    nost_y = sum_y / (int32_t)LSM6DSV16X_ST_N_AVG;
    nost_z = sum_z / (int32_t)LSM6DSV16X_ST_N_AVG;
    debug_log_printf("LSM6DSV16X self-test: no-st avg x=%d y=%d z=%d\r\n",
                     (int)nost_x,
                     (int)nost_y,
                     (int)nost_z);

    /* Step 6 – Enable positive accelerometer self-test: CTRL10.ST_XL = 01b */
    reg_update(device, LSM6DSV16X_CTRL10,
               LSM6DSV16X_CTRL10_ST_XL_MASK,
               LSM6DSV16X_CTRL10_ST_XL_POS);
    reg_read(device, LSM6DSV16X_CTRL10, &ctrl10);
    debug_log_printf("LSM6DSV16X self-test: CTRL10 after enable 0x%x\r\n",
                     ctrl10);

    /* Step 7 – Settle, then flush */
    stopMilliseconds(100U);
    rc = st_read_sample(device, &ax, &ay, &az);
    if (rc != 0) {
        debug_log_printf("LSM6DSV16X self-test: timeout waiting for ST flush sample\r\n");
        result = LSM6DSV16X_SELF_TEST_FAIL_TO;
        goto cleanup;
    }

    /* Step 8 – Average with self-test (ST) */
    sum_x = 0; sum_y = 0; sum_z = 0;
    for (s = 0U; s < LSM6DSV16X_ST_N_AVG; s++) {
        rc = st_read_sample(device, &ax, &ay, &az);
        if (rc != 0) {
            debug_log_printf("LSM6DSV16X self-test: timeout during ST sample %u\r\n",
                             (unsigned)s);
            result = LSM6DSV16X_SELF_TEST_FAIL_TO;
            goto cleanup;
        }
        sum_x += ax; sum_y += ay; sum_z += az;
    }
    st_x = sum_x / (int32_t)LSM6DSV16X_ST_N_AVG;
    st_y = sum_y / (int32_t)LSM6DSV16X_ST_N_AVG;
    st_z = sum_z / (int32_t)LSM6DSV16X_ST_N_AVG;
    debug_log_printf("LSM6DSV16X self-test: st avg x=%d y=%d z=%d\r\n",
                     (int)st_x,
                     (int)st_y,
                     (int)st_z);

    /* Step 9 – Disable self-test */
    reg_update(device, LSM6DSV16X_CTRL10,
               LSM6DSV16X_CTRL10_ST_XL_MASK,
               LSM6DSV16X_CTRL10_ST_XL_OFF);

    /* Step 11 – Evaluate deltas */
    delta_x = st_x - nost_x;  if (delta_x < 0) { delta_x = -delta_x; }
    delta_y = st_y - nost_y;  if (delta_y < 0) { delta_y = -delta_y; }
    delta_z = st_z - nost_z;  if (delta_z < 0) { delta_z = -delta_z; }
    debug_log_printf("LSM6DSV16X self-test: delta x=%d y=%d z=%d\r\n",
                     (int)delta_x,
                     (int)delta_y,
                     (int)delta_z);

    if (delta_x < LSM6DSV16X_ST_XL_MIN_LSB || delta_x > LSM6DSV16X_ST_XL_MAX_LSB) {
        st_log_delta_failure("X", delta_x);
        result = LSM6DSV16X_SELF_TEST_FAIL_X;
        goto cleanup;
    }
    if (delta_y < LSM6DSV16X_ST_XL_MIN_LSB || delta_y > LSM6DSV16X_ST_XL_MAX_LSB) {
        st_log_delta_failure("Y", delta_y);
        result = LSM6DSV16X_SELF_TEST_FAIL_Y;
        goto cleanup;
    }
    if (delta_z < LSM6DSV16X_ST_XL_MIN_LSB || delta_z > LSM6DSV16X_ST_XL_MAX_LSB) {
        st_log_delta_failure("Z", delta_z);
        result = LSM6DSV16X_SELF_TEST_FAIL_Z;
        goto cleanup;
    }

cleanup:
    if (result == LSM6DSV16X_SELF_TEST_PASS) {
        debug_log_printf("LSM6DSV16X self-test: pass dx=%d dy=%d dz=%d\r\n",
                         (int)delta_x,
                         (int)delta_y,
                         (int)delta_z);
    } else {
        debug_log_printf("LSM6DSV16X self-test: fail result %d\r\n",
                         (int)result);
    }
    reg_update(device, LSM6DSV16X_CTRL10,
               LSM6DSV16X_CTRL10_ST_XL_MASK,
               LSM6DSV16X_CTRL10_ST_XL_OFF);
    shutdown_registers(device);
    device_end(device);
    device_power_off(device);
    return result;
}

/* =========================================================================
 * Status and direct-read functions
 * ====================================================================== */

int lsm6dsv16x_read_accel(const TagLsm6dsv16xDevice *device,
                          int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t raw[6];
    uint8_t status = 0;
    int result = -1;

    if (!valid_device(device) || x == NULL || y == NULL || z == NULL) {
        return -1;
    }

    device_power_on(device);
    device_begin(device);

    if (reg_read(device, LSM6DSV16X_STATUS_REG, &status) != 0 ||
        (status & 0x01U) == 0U) {
        goto done;
    }

    if (reg_read_block(device, LSM6DSV16X_OUTX_L_A, raw, 6U) != 0) {
        goto done;
    }
    *x = (int16_t)((uint16_t)raw[1] << 8U | raw[0]);
    *y = (int16_t)((uint16_t)raw[3] << 8U | raw[2]);
    *z = (int16_t)((uint16_t)raw[5] << 8U | raw[4]);
    result = 0;

done:
    device_end(device);
    return result;
}

uint8_t lsm6dsv16x_read_wakeup_src(const TagLsm6dsv16xDevice *device)
{
    uint8_t src = 0;

    if (!valid_device(device)) { return 0U; }

    device_power_on(device);
    device_begin(device);
    reg_read(device, LSM6DSV16X_WAKE_UP_SRC, &src);
    device_end(device);
    return src;
}

uint16_t lsm6dsv16x_read_fifo_status(const TagLsm6dsv16xDevice *device,
                                     uint8_t *wtm_flag, uint8_t *ovr_flag)
{
    uint8_t  st1 = 0;
    uint8_t  st2 = 0;
    uint16_t level;

    if (!valid_device(device)) { return 0U; }

    device_power_on(device);
    device_begin(device);
    reg_read(device, LSM6DSV16X_FIFO_STATUS1, &st1);
    reg_read(device, LSM6DSV16X_FIFO_STATUS2, &st2);
    device_end(device);

    level = (uint16_t)(((uint16_t)(st2 & 0x03U) << 8U) | (uint16_t)st1);

    if (wtm_flag != NULL) { *wtm_flag = (st2 >> 7U) & 0x01U; }
    if (ovr_flag != NULL) { *ovr_flag = (st2 >> 6U) & 0x01U; }

    return level;
}
