#include "lps22hh.h"
#include "hal.h"
#include "custom.h"

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

/* STATUS sensitivity */

#define LPS22HH_PRESSURE_SENSITIVITY ((float) 1.0/4096)
#define LPS22HH_TEMPERATURE_SENSITIVITY ((float) 1.0/100)

/* Write one register. */
static int write_reg(const TagPressureDevice *device, uint8_t reg, uint8_t val)
{
    return tagRegisterWrite(device->registers, reg, &val, 1);
}

/*
 * Read a register block using the sensor's auto-increment mechanism.
 *
 * For SPI, the MSB of the register address is set to request incrementing
 * address access across successive bytes.
 */
static int read_block(const TagPressureDevice *device, uint8_t reg,
                      uint8_t *buf, uint16_t len)
{
    return tagRegisterRead(device->registers, reg, buf, len);
}

/*
 * Read-modify-write helper.
 *
 * This preserves unrelated fields in a register when only one bitfield needs
 * to change.
 */
static int update_reg(const TagPressureDevice *device, uint8_t reg,
                      uint8_t mask, uint8_t value)
{
    uint8_t cur = 0;
    if (read_block(device, reg, &cur, 1) != 0) {
        return -1;
    }

    cur = (uint8_t)((cur & (uint8_t)~mask) | (value & mask));
    return write_reg(device, reg, cur);
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
 * 
 * NOTE: does not turn on spi
 */
static int lps22hh_config_int_drdy(const TagPressureDevice *device)
{
    uint8_t ctrl2 = (uint8_t)(LPS22HH_CTRL2_IF_ADD_INC);
    uint8_t ctrl3 = (uint8_t)(LPS22HH_CTRL3_DRDY);

    write_reg(device, LPS22HH_REG_CTRL_REG2, ctrl2);
    write_reg(device, LPS22HH_REG_CTRL_REG3, ctrl3);

    return 0;
}

bool lps22hh_check_who_am_i_device(const TagPressureDevice *device)
{
    uint8_t v = 0;

    tagPressureDeviceBegin(device);
    read_block(device, LPS22HH_REG_WHO_AM_I, &v, 1);
    tagPressureDeviceEnd(device);
    return v == LPS22HH_WHO_AM_I_VAL;
}

int lps22hh_set_idle_device(const TagPressureDevice *device)
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

    tagPressureDeviceBegin(device);

    write_reg(device, LPS22HH_REG_CTRL_REG2, ctrl2);
    write_reg(device, LPS22HH_REG_CTRL_REG3, ctrl3);
    write_reg(device, LPS22HH_REG_CTRL_REG1, ctrl1);
    
    tagPressureDeviceEnd(device);
    return 0;
}

int lps22hh_config_continuous_device(const TagPressureDevice *device,
                                     lps22hh_odr_t odr, lps22hh_lpf_t lpf)
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

    tagPressureDeviceBegin(device);

    lps22hh_config_int_drdy(device);
    write_reg(device, LPS22HH_REG_CTRL_REG1, ctrl1);
    tagPressureDeviceEnd(device);
    return 0;
}

int lps22hh_config_triggered_device(const TagPressureDevice *device,
                                    lps22hh_lpf_t lpf)
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

    tagPressureDeviceBegin(device);

    lps22hh_config_int_drdy(device);
    write_reg(device, LPS22HH_REG_CTRL_REG1, ctrl1);

    tagPressureDeviceEnd(device);
    return 0;
}

int lps22hh_trigger_one_shot_device(const TagPressureDevice *device)
{
    /*
     * The ONE_SHOT bit starts a single conversion when ODR = 0.
     * The device clears the bit automatically after the measurement is taken.
     */

    tagPressureDeviceBegin(device);
    update_reg(device, LPS22HH_REG_CTRL_REG2,
               LPS22HH_CTRL2_ONE_SHOT,
               LPS22HH_CTRL2_ONE_SHOT);
    tagPressureDeviceEnd(device);
    return 0;
}

bool lps22hh_data_ready_device(const TagPressureDevice *device)
{
    /*
     * The status register reports whether pressure and/or temperature samples
     * are available. This is useful both in polling mode and for sanity checks
     * after an interrupt.
     */
    uint8_t s = 0;
    tagPressureDeviceBegin(device);
    read_block(device, LPS22HH_REG_STATUS, &s, 1);
    tagPressureDeviceEnd(device);

    return (s & (LPS22HH_STATUS_P_DA | LPS22HH_STATUS_T_DA)) != 0;
}

int lps22hh_read_raw_device(const TagPressureDevice *device, int32_t *pressure,
                            int32_t *temperature)
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
    tagPressureDeviceBegin(device);
    read_block(device, LPS22HH_REG_PRESS_OUT_XL, b, 5);
    tagPressureDeviceEnd(device);

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
/* Converstion Routines*/


float lps22hh_raw_pressure_hPA(int32_t pressure){
    return pressure * LPS22HH_PRESSURE_SENSITIVITY;
}
float lps22hh_raw_temperature_c(int32_t temperature){
    return temperature *LPS22HH_TEMPERATURE_SENSITIVITY;
}
