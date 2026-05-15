/*
 * lps22xx.c
 *
 * Implementation of the LPS22HH and LPS22DF drivers declared in lps22xx.h.
 *
 * The code is structured around three small helper layers:
 *   1) generic SPI register access helpers
 *   2) LPS22HH-specific configuration helpers
 *   3) LPS22DF-specific configuration helpers
 *
 * The register definitions and public API live in lps22xx.h.
 */

#include "lps22xx.h"

/* --------------------------------------------------------------------------
 * Common SPI helpers
 * --------------------------------------------------------------------------
 */

/* Write one register. */
static int write_reg(uint8_t reg, uint8_t val)
{
    return spi_write(reg, &val, 1);
}

/* Read one register. */
static int read_reg(uint8_t reg, uint8_t *val)
{
    return spi_read(reg, val, 1);
}

/*
 * Read a register block using the sensor's auto-increment mechanism.
 *
 * For SPI, the MSB of the register address is set to request incrementing
 * address access across successive bytes.
 */
static int read_block(uint8_t reg, uint8_t *buf, uint16_t len)
{
    return spi_read((uint8_t)(reg | 0x80u), buf, len);
}

/*
 * Read-modify-write helper.
 *
 * This preserves unrelated fields in a register when only one bitfield needs
 * to change.
 */
static int update_reg(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t cur = 0;
    if (read_reg(reg, &cur) != 0) {
        return -1;
    }

    cur = (uint8_t)((cur & (uint8_t)~mask) | (value & mask));
    return write_reg(reg, cur);
}

/* --------------------------------------------------------------------------
 * LPS22HH implementation
 * --------------------------------------------------------------------------
 */

/*
 * Configure the LPS22HH INT/DRDY pin for data-ready signaling.
 *
 * The goal is to expose a stable data-ready indication without requiring the
 * application to perform a manual interrupt-latch reset.
 *
 * The configuration uses:
 *   - auto-increment enabled for multi-byte SPI reads
 *   - DRDY routed to the interrupt pin
 *   - default active-high, push-pull behavior
 */
static int lps22hh_config_int_drdy(void)
{
    uint8_t ctrl2 = (uint8_t)(LPS22HH_CTRL2_IF_ADD_INC);
    uint8_t ctrl3 = (uint8_t)(LPS22HH_CTRL3_DRDY);

    if (write_reg(LPS22HH_REG_CTRL_REG2, ctrl2) != 0) {
        return -1;
    }

    return write_reg(LPS22HH_REG_CTRL_REG3, ctrl3);
}

bool lps22hh_check_who_am_i(void)
{
    uint8_t v = 0;
    if (read_reg(LPS22HH_REG_WHO_AM_I, &v) != 0) {
        return false;
    }
    return v == LPS22HH_WHO_AM_I_VAL;
}

int lps22hh_set_idle(void)
{
    /*
     * Idle / lowest-power state:
     *   - ODR = 0 (power-down)
     *   - BDU = 1 to keep multi-byte reads coherent if data is later read
     *
     * We also preserve the read auto-increment setting because it is harmless
     * and convenient for later burst reads.
     */
    uint8_t ctrl1 = LPS22HH_CTRL1_BDU;
    uint8_t ctrl2 = LPS22HH_CTRL2_IF_ADD_INC;
    uint8_t ctrl3 = 0;

    if (write_reg(LPS22HH_REG_CTRL_REG2, ctrl2) != 0) {
        return -1;
    }
    if (write_reg(LPS22HH_REG_CTRL_REG3, ctrl3) != 0) {
        return -1;
    }
    return write_reg(LPS22HH_REG_CTRL_REG1, ctrl1);
}

int lps22hh_config_continuous(lps22hh_odr_t odr, lps22hh_lpf_t lpf)
{
    if ((unsigned)odr > (unsigned)LPS22HH_ODR_200HZ) {
        return -1;
    }

    /*
     * CTRL_REG1 layout:
     *   - ODR[2:0] selects the output data rate
     *   - EN_LPFP enables the low-pass filter
     *   - LPFP_CFG selects the filter option
     *   - BDU ensures coherent reads across the 5-byte pressure/temperature burst
     */
    uint8_t ctrl1 = LPS22HH_CTRL1_BDU;
    ctrl1 |= (uint8_t)(((uint8_t)odr << 4) & LPS22HH_CTRL1_ODR_MASK);

    if (lpf != LPS22HH_LPF_DISABLE) {
        ctrl1 |= LPS22HH_CTRL1_EN_LPFP;
        ctrl1 |= (uint8_t)(((uint8_t)lpf << 2) & LPS22HH_CTRL1_LPFP_CFG_MASK);
    }

    if (lps22hh_config_int_drdy() != 0) {
        return -1;
    }

    return write_reg(LPS22HH_REG_CTRL_REG1, ctrl1);
}

int lps22hh_config_triggered(lps22hh_lpf_t lpf)
{
    /*
     * One-shot mode configuration:
     *   - ODR = 0 until the application triggers ONE_SHOT
     *   - DRDY output remains enabled so the application can wait on the pin
     *   - BDU stays enabled for coherent output reads
     *
     * The LPS22HH low-pass selection is retained for API symmetry. If the
     * target revision treats the filter as continuous-mode-only, the caller can
     * simply pass LPS22HH_LPF_DISABLE.
     */
    uint8_t ctrl1 = LPS22HH_CTRL1_BDU;

    (void)lpf;

    if (lps22hh_config_int_drdy() != 0) {
        return -1;
    }

    return write_reg(LPS22HH_REG_CTRL_REG1, ctrl1);
}

int lps22hh_trigger_one_shot(void)
{
    /*
     * The ONE_SHOT bit starts a single conversion when ODR = 0.
     * The device clears the bit automatically after the measurement is taken.
     */
    return update_reg(LPS22HH_REG_CTRL_REG2,
                      LPS22HH_CTRL2_ONE_SHOT,
                      LPS22HH_CTRL2_ONE_SHOT);
}

bool lps22hh_data_ready(void)
{
    /*
     * The status register reports whether pressure and/or temperature samples
     * are available. This is useful both in polling mode and for sanity checks
     * after an interrupt.
     */
    uint8_t s = 0;
    if (read_reg(LPS22HH_REG_STATUS, &s) != 0) {
        return false;
    }
    return (s & (LPS22HH_STATUS_P_DA | LPS22HH_STATUS_T_DA)) != 0;
}

int lps22hh_read_raw(int32_t *pressure, int32_t *temperature)
{
    if ((pressure == NULL) || (temperature == NULL)) {
        return -1;
    }

    /*
     * Pressure and temperature are read in one burst so the sample pair is
     * coherent. The sensor returns:
     *   - pressure: 3 bytes, little-endian, 24-bit signed value
     *   - temperature: 2 bytes, little-endian, 16-bit signed value
     */
    uint8_t b[5] = {0};
    if (read_block(LPS22HH_REG_PRESS_OUT_XL, b, 5) != 0) {
        return -1;
    }

    int32_t p = (int32_t)(((uint32_t)b[2] << 16) |
                          ((uint32_t)b[1] << 8) |
                          b[0]);
    if (p & 0x00800000) {
        p |= (int32_t)0xFF000000;
    }

    int16_t t = (int16_t)(((uint16_t)b[4] << 8) | b[3]);

    *pressure = p;
    *temperature = (int32_t)t;
    return 0;
}

/* --------------------------------------------------------------------------
 * LPS22DF implementation
 * --------------------------------------------------------------------------
 */

/*
 * Configure the LPS22DF INT/DRDY pin to output data-ready information.
 *
 * CTRL_REG3 controls pin polarity / pad type / auto-increment.
 * CTRL_REG4 enables the DRDY function on the interrupt pin.
 *
 * Parameters:
 *   active_low  - true for active-low interrupt polarity
 *   open_drain  - true for open-drain output, false for push-pull
 */
static int lps22df_config_int_drdy(bool active_low, bool open_drain)
{
    uint8_t ctrl3 = LPS22DF_CTRL3_IF_ADD_INC;
    if (active_low) {
        ctrl3 |= LPS22DF_CTRL3_INT_H_L;
    }
    if (open_drain) {
        ctrl3 |= LPS22DF_CTRL3_PP_OD;
    }

    /*
     * CTRL_REG4: enable data-ready interrupt output.
     * DRDY is level-signaled, so no explicit status reset is required by the
     * application for normal operation.
     */
    uint8_t ctrl4 = (uint8_t)(LPS22DF_CTRL4_INT_EN | LPS22DF_CTRL4_DRDY);

    if (write_reg(LPS22DF_REG_CTRL_REG3, ctrl3) != 0) {
        return -1;
    }
    return write_reg(LPS22DF_REG_CTRL_REG4, ctrl4);
}

bool lps22df_check_who_am_i(void)
{
    uint8_t v = 0;
    if (read_reg(LPS22DF_REG_WHO_AM_I, &v) != 0) {
        return false;
    }
    return v == LPS22DF_WHO_AM_I_VAL;
}

int lps22df_set_idle(void)
{
    /*
     * Idle / lowest-power state:
     *   - ODR = 0
     *   - averaging field cleared
     *   - BDU remains enabled
     *   - DRDY interrupt disabled
     */
    uint8_t ctrl1 = 0;
    uint8_t ctrl2 = (uint8_t)(LPS22DF_CTRL2_BDU);
    uint8_t ctrl3 = (uint8_t)(LPS22DF_CTRL3_IF_ADD_INC);
    uint8_t ctrl4 = 0;

    if (write_reg(LPS22DF_REG_CTRL_REG2, ctrl2) != 0) {
        return -1;
    }
    if (write_reg(LPS22DF_REG_CTRL_REG3, ctrl3) != 0) {
        return -1;
    }
    if (write_reg(LPS22DF_REG_CTRL_REG4, ctrl4) != 0) {
        return -1;
    }
    return write_reg(LPS22DF_REG_CTRL_REG1, ctrl1);
}

/*
 * Write the LPS22DF CTRL_REG1 fields that select ODR and averaging.
 *
 * CTRL_REG1 layout used here:
 *   - ODR[3:0] selects the sensor acquisition rate
 *   - AVG[2:0] selects the oversampling/averaging amount
 */
static int lps22df_write_ctrl1(lps22df_odr_t odr, lps22df_avg_t avg)
{
    if ((unsigned)odr > (unsigned)LPS22DF_ODR_200HZ) {
        return -1;
    }

    uint8_t ctrl1 = 0;
    ctrl1 |= (uint8_t)(((uint8_t)odr << 3) & LPS22DF_CTRL1_ODR_MASK);
    ctrl1 |= (uint8_t)(((uint8_t)avg << 0) & LPS22DF_CTRL1_AVG_MASK);
    return write_reg(LPS22DF_REG_CTRL_REG1, ctrl1);
}

/*
 * Write the LPS22DF CTRL_REG2 filter/BDU bits.
 *
 * The LPF selection is controlled by EN_LPFP and LFPF_CFG.
 * BDU is always enabled for coherent burst reads.
 */
static int lps22df_write_ctrl2(bool lpf_enable, lps22df_lpf_t lpf)
{
    uint8_t ctrl2 = LPS22DF_CTRL2_BDU;

    if (lpf_enable) {
        ctrl2 |= LPS22DF_CTRL2_EN_LPFP;
        if (lpf == LPS22DF_LPF_ODR_DIV_9) {
            ctrl2 |= LPS22DF_CTRL2_LFPF_CFG;
        }
    }

    return write_reg(LPS22DF_REG_CTRL_REG2, ctrl2);
}

int lps22df_config_continuous(lps22df_odr_t odr, lps22df_lpf_t lpf, lps22df_avg_t avg)
{
    /* Continuous mode requires a non-zero ODR. */
    if ((unsigned)odr == (unsigned)LPS22DF_ODR_ONE_SHOT) {
        return -1;
    }

    /* Route DRDY to the INT pin and keep the output level-based. */
    if (lps22df_config_int_drdy(false, false) != 0) {
        return -1;
    }

    if (lps22df_write_ctrl2(true, lpf) != 0) {
        return -1;
    }

    return lps22df_write_ctrl1(odr, avg);
}

int lps22df_config_triggered(lps22df_lpf_t lpf, lps22df_avg_t avg)
{
    /*
     * Triggered mode / one-shot mode:
     *   - ODR remains 0 until the application starts a conversion
     *   - DRDY is routed to INT/DRDY so the application can wait on the pin
     *   - averaging is still selectable so the caller can trade speed vs noise
     */
    if (lps22df_config_int_drdy(false, false) != 0) {
        return -1;
    }

    if (lps22df_write_ctrl2(true, lpf) != 0) {
        return -1;
    }

    return lps22df_write_ctrl1(LPS22DF_ODR_ONE_SHOT, avg);
}

int lps22df_trigger_one_shot(void)
{
    /*
     * ONESHOT starts a single conversion when ODR is 0.
     * The bit is self-clearing once the conversion begins/completes.
     */
    return update_reg(LPS22DF_REG_CTRL_REG2,
                      LPS22DF_CTRL2_ONESHOT,
                      LPS22DF_CTRL2_ONESHOT);
}

bool lps22df_data_ready(void)
{
    uint8_t s = 0;
    if (read_reg(LPS22DF_REG_STATUS, &s) != 0) {
        return false;
    }
    return (s & (LPS22DF_STATUS_P_DA | LPS22DF_STATUS_T_DA)) != 0;
}

int lps22df_read_raw(int32_t *pressure, int32_t *temperature)
{
    if ((pressure == NULL) || (temperature == NULL)) {
        return -1;
    }

    /*
     * Burst read the pressure and temperature sample pair.
     * The sensor stores pressure as a 24-bit signed quantity and temperature
     * as a 16-bit signed quantity.
     */
    uint8_t b[5] = {0};
    if (read_block(LPS22DF_REG_PRESS_OUT_XL, b, 5) != 0) {
        return -1;
    }

    int32_t p = (int32_t)(((uint32_t)b[2] << 16) |
                          ((uint32_t)b[1] << 8) |
                          b[0]);
    if (p & 0x00800000) {
        p |= (int32_t)0xFF000000;
    }

    int16_t t = (int16_t)(((uint16_t)b[4] << 8) | b[3]);

    *pressure = p;
    *temperature = (int32_t)t;
    return 0;
}
