/**
 * @file ak09940.c
 * @brief AK09940A magnetometer driver implementation for ChibiOS HAL.
 *
 * Implementation notes:
 * - All register access is done over SPI with explicit burst transactions.
 * - The sensor's data path is 18-bit per axis and the final byte of the burst
 *   must be ST2, which releases the internal output latch.
 * - The DRDY/TRG line is handled differently depending on operating mode:
 *     * continuous mode  -> MCU configures the line as input
 *     * triggered mode   -> MCU configures the line as output
 *
 * This file is written to be easy to integrate into a production firmware
 * project, while still remaining compact enough to adapt.
 */

#include "ak09940.h"

/* ------------------------- Register map ------------------------- */
#define REG_WIA1      0x00
#define REG_WIA2      0x01

#define REG_ST1       0x10
#define REG_HXL       0x11
#define REG_HXM       0x12
#define REG_HXH       0x13
#define REG_HYL       0x14
#define REG_HYM       0x15
#define REG_HYH       0x16
#define REG_HZL       0x17
#define REG_HZM       0x18
#define REG_HZH       0x19
#define REG_TMPS      0x1A
#define REG_ST2       0x1B

#define REG_CNTL1     0x30
#define REG_CNTL2     0x31
#define REG_CNTL3     0x32
#define REG_CNTL4     0x33

#define WIA2_EXPECTED 0x48

/* Status bits */
#define ST1_DRDY      0x01
#define ST2_HOFL      0x08
#define ST2_INV       0x10

/* CNTL1 bitfields */
#define CNTL1_MT2     0x80
#define CNTL1_DTSET   0x20

/* CNTL2 bitfields */
#define CNTL2_TEM     0x40

/* CNTL3 mode and drive-select fields.
 * The exact bit assignments should be kept in sync with the datasheet.
 * This driver uses a compact abstraction layer to make firmware clearer.
 */
#define CNTL3_MODE_MASK 0x1F
#define MODE_POWER_DOWN 0x00
#define MODE_SINGLE     0x01
#define MODE_CONT1      0x02
#define MODE_CONT2      0x04
#define MODE_CONT3      0x06
#define MODE_CONT4      0x08
#define MODE_CONT5      0x0A
#define MODE_CONT6      0x0C
#define MODE_CONT7      0x0E
#define MODE_CONT8      0x0F
#define MODE_EXT_TRG    0x18
#define MODE_SELF_TEST  0x10

typedef struct {
    volatile bool drdy_flag;   /* Latched by ISR wrapper. */
    ak09940_drdy_cb_t user_cb; /* Optional user callback. */
    void *user_arg;            /* Callback argument. */
} ak09940_state_t;

/* The driver stores a tiny state blob behind user_arg to avoid changing the
 * public object layout with additional hidden fields.
 */
static ak09940_state_t *state(ak09940_t *dev)
{
    return (ak09940_state_t *)dev->user_arg;
}

/* Convert human-friendly rate enum to the device's continuous-mode code. */
static uint8_t rate_to_mode(ak09940_rate_t rate)
{
    switch (rate) {
    case AK09940_RATE_10HZ:  return MODE_CONT1;
    case AK09940_RATE_20HZ:  return MODE_CONT2;
    case AK09940_RATE_50HZ:  return MODE_CONT3;
    case AK09940_RATE_100HZ: return MODE_CONT4;
    case AK09940_RATE_200HZ: return MODE_CONT5;
    case AK09940_RATE_400HZ: return MODE_CONT6;
    default:                 return MODE_CONT1;
    }
}

/* Build the drive-select values used by CNTL1/CNTL3.
 *
 * The intent is to keep the public API stable and readable, while the exact
 * register bit combinations remain isolated in one place.
 */
static void pack_drive(ak09940_drive_t drive, uint8_t *cntl1, uint8_t *cntl3)
{
    *cntl1 = 0U;
    *cntl3 = 0U;

    switch (drive) {
    case AK09940_DRIVE_LOW_POWER_1:
        *cntl3 = 0x00;
        break;
    case AK09940_DRIVE_LOW_POWER_2:
        *cntl3 = 0x01;
        break;
    case AK09940_DRIVE_LOW_NOISE_1:
        *cntl3 = 0x02;
        break;
    case AK09940_DRIVE_LOW_NOISE_2:
        *cntl3 = 0x03;
        break;
    case AK09940_DRIVE_ULTRA_LOW_POWER:
        *cntl1 = CNTL1_MT2;
        *cntl3 = 0x00;
        break;
    default:
        *cntl3 = 0x00;
        break;
    }
}

/* Low-level SPI helpers.
 *
 * ChibiOS SPI transactions are wrapped here so the higher-level code reads like
 * a straightforward sensor driver rather than a bus-transaction script.
 */
static msg_t spi_write_regs(ak09940_t *dev, uint8_t reg, const uint8_t *buf, size_t n)
{
    uint8_t hdr = reg & 0x7FU; /* write transaction */

    spiAcquireBus(dev->spip);
    spiSelect(dev->spip);

    spiSend(dev->spip, 1, &hdr);
    if (n != 0U) {
        spiSend(dev->spip, n, buf);
    }

    spiUnselect(dev->spip);
    spiReleaseBus(dev->spip);
    return MSG_OK;
}

static msg_t spi_read_regs(ak09940_t *dev, uint8_t reg, uint8_t *buf, size_t n)
{
    uint8_t hdr = (uint8_t)(0x80U | (reg & 0x7FU)); /* read transaction */

    spiAcquireBus(dev->spip);
    spiSelect(dev->spip);

    spiSend(dev->spip, 1, &hdr);
    spiReceive(dev->spip, n, buf);

    spiUnselect(dev->spip);
    spiReleaseBus(dev->spip);
    return MSG_OK;
}

/* 18-bit signed value reconstruction.
 *
 * The AK09940A returns 18-bit axis samples across three registers:
 *   low byte, middle byte, high byte
 *
 * Only the lower two bits of the high byte are part of the sample. The helper
 * sign-extends from bit 17 so the result can be safely stored in int32_t.
 */
static int32_t convert_18bit(uint8_t l, uint8_t m, uint8_t h)
{
    int32_t raw = ((int32_t)(h & 0x03U) << 16) | ((int32_t)m << 8) | l;
    if (raw & 0x20000) {
        raw |= (int32_t)0xFFFC0000;
    }
    return raw;
}

void ak09940ObjectInit(ak09940_t *dev, SPIDriver *spip, const SPIConfig *spicfg, ioline_t drdy_trg_line)
{
    static ak09940_state_t s;

    dev->spip = spip;
    dev->spicfg = spicfg;
    dev->drdy_trg_line = drdy_trg_line;
    dev->irq_enabled = false;

    s.drdy_flag = false;
    s.user_cb = NULL;
    s.user_arg = NULL;
    dev->user_arg = &s;
}

void ak09940SetDrdyCallback(ak09940_t *dev, ak09940_drdy_cb_t cb, void *arg)
{
    ak09940_state_t *s = state(dev);
    s->user_cb = cb;
    s->user_arg = arg;
}

bool ak09940_check_whoami(ak09940_t *dev)
{
    uint8_t v = 0U;
    if (spi_read_regs(dev, REG_WIA2, &v, 1U) != MSG_OK) {
        return false;
    }
    return (v == WIA2_EXPECTED);
}

/* Power-down is the safest state for configuration changes. */
msg_t ak09940_init_power_down(ak09940_t *dev)
{
    uint8_t cntl3 = MODE_POWER_DOWN;
    return spi_write_regs(dev, REG_CNTL3, &cntl3, 1U);
}

/* Continuous mode:
 * - DRDY/TRG pin becomes an input from the MCU side.
 * - Firmware can poll or use an EXTI event from that line.
 * - The sensor produces samples at the requested ODR.
 */
msg_t ak09940_init_continuous(ak09940_t *dev,
                              ak09940_rate_t rate,
                              ak09940_drive_t drive,
                              ak09940_temp_mode_t temp_mode)
{
    uint8_t cntl1, cntl2, cntl3;

    /* In continuous mode, the MCU should treat the pin as an input. */
    palSetLineMode(dev->drdy_trg_line, PAL_MODE_INPUT_PULLUP);

    pack_drive(drive, &cntl1, &cntl3);
    cntl2 = (temp_mode == AK09940_TEMP_ENABLED) ? CNTL2_TEM : 0U;
    cntl3 |= rate_to_mode(rate);

    if (spi_write_regs(dev, REG_CNTL1, &cntl1, 1U) != MSG_OK) return MSG_RESET;
    if (spi_write_regs(dev, REG_CNTL2, &cntl2, 1U) != MSG_OK) return MSG_RESET;
    if (spi_write_regs(dev, REG_CNTL3, &cntl3, 1U) != MSG_OK) return MSG_RESET;

    return MSG_OK;
}

/* Triggered mode:
 * - DRDY/TRG pin becomes an output from the MCU side.
 * - Firmware pulses the line to request a measurement.
 * - The sensor then performs a single conversion using the configured drive.
 */
msg_t ak09940_init_triggered(ak09940_t *dev,
                             ak09940_drive_t drive,
                             ak09940_temp_mode_t temp_mode)
{
    uint8_t cntl1, cntl2, cntl3;

    palSetLineMode(dev->drdy_trg_line, PAL_MODE_OUTPUT_PUSHPULL);
    palClearLine(dev->drdy_trg_line);

    pack_drive(drive, &cntl1, &cntl3);
    cntl1 |= CNTL1_DTSET;      /* trigger input path select */
    cntl2 = (temp_mode == AK09940_TEMP_ENABLED) ? CNTL2_TEM : 0U;
    cntl3 |= MODE_EXT_TRG;

    if (spi_write_regs(dev, REG_CNTL1, &cntl1, 1U) != MSG_OK) return MSG_RESET;
    if (spi_write_regs(dev, REG_CNTL2, &cntl2, 1U) != MSG_OK) return MSG_RESET;
    if (spi_write_regs(dev, REG_CNTL3, &cntl3, 1U) != MSG_OK) return MSG_RESET;

    return MSG_OK;
}

void ak09940_enable_drdy_interrupt(ak09940_t *dev, bool enable)
{
    ak09940_state_t *s = state(dev);

    s->drdy_flag = false;
    dev->irq_enabled = enable;

    if (enable) {
        palSetLineMode(dev->drdy_trg_line, PAL_MODE_INPUT_PULLUP);
        palEnableLineEvent(dev->drdy_trg_line, PAL_EVENT_MODE_RISING_EDGE);
    } else {
        palDisableLineEvent(dev->drdy_trg_line);
    }
}

/* Lightweight ISR hook. Call this from your board EXTI handler. */
void ak09940_handle_drdy_isr(ak09940_t *dev)
{
    ak09940_state_t *s = state(dev);
    s->drdy_flag = true;

    if (s->user_cb != NULL) {
        s->user_cb(s->user_arg);
    }
}

bool ak09940_data_ready(ak09940_t *dev)
{
    ak09940_state_t *s = state(dev);

    if (dev->irq_enabled) {
        if (s->drdy_flag) {
            return true;
        }
        return (palReadLine(dev->drdy_trg_line) == PAL_HIGH);
    }

    uint8_t st1 = 0U;
    if (spi_read_regs(dev, REG_ST1, &st1, 1U) != MSG_OK) {
        return false;
    }

    return ((st1 & ST1_DRDY) != 0U);
}

bool ak09940_read_sample(ak09940_t *dev,
                         int32_t *mx_raw,
                         int32_t *my_raw,
                         int32_t *mz_raw,
                         int16_t *temp_raw)
{
    uint8_t st1 = 0U;
    uint8_t buf[11];

    /* ST1 confirms that a new sample is ready. */
    if (spi_read_regs(dev, REG_ST1, &st1, 1U) != MSG_OK) {
        return false;
    }
    if ((st1 & ST1_DRDY) == 0U) {
        return false;
    }

    /*
     * Burst read from HXL through ST2.
     *
     * The order is:
     *   HXL, HXM, HXH, HYL, HYM, HYH, HZL, HZM, HZH, TMPS, ST2
     *
     * ST2 must be the final byte in the burst. This is what releases the
     * sensor's internal latch and allows the next conversion to be exposed.
     */
    if (spi_read_regs(dev, REG_HXL, buf, sizeof(buf)) != MSG_OK) {
        return false;
    }

    *mx_raw = convert_18bit(buf[0], buf[1], buf[2]);
    *my_raw = convert_18bit(buf[3], buf[4], buf[5]);
    *mz_raw = convert_18bit(buf[6], buf[7], buf[8]);
    *temp_raw = (int16_t)(int8_t)buf[9];

    /* Overrange/invalidation flags live in ST2. */
    if ((buf[10] & (ST2_HOFL | ST2_INV)) != 0U) {
        return false;
    }

    state(dev)->drdy_flag = false;
    return true;
}

bool ak09940_self_test(ak09940_t *dev)
{
    uint8_t cntl1, cntl2, cntl3;
    int32_t mx, my, mz;
    int16_t temp;

    /*
     * A practical self-test sequence:
     * 1) configure a known-good drive selection
     * 2) enable temperature if desired
     * 3) place the part into self-test mode
     * 4) wait for conversion
     * 5) read one sample and compare against acceptance limits
     *
     * Replace the placeholder acceptance limits below with exact datasheet
     * values if your project requires strict certification-level validation.
     */
    pack_drive(AK09940_DRIVE_LOW_NOISE_2, &cntl1, &cntl3);
    cntl2 = CNTL2_TEM;
    cntl3 = (uint8_t)(cntl3 | MODE_SELF_TEST);

    if (spi_write_regs(dev, REG_CNTL1, &cntl1, 1U) != MSG_OK) return false;
    if (spi_write_regs(dev, REG_CNTL2, &cntl2, 1U) != MSG_OK) return false;
    if (spi_write_regs(dev, REG_CNTL3, &cntl3, 1U) != MSG_OK) return false;

    chThdSleepMilliseconds(10);

    if (!ak09940_read_sample(dev, &mx, &my, &mz, &temp)) {
        return false;
    }

    if (mx < -20000 || mx > 20000) return false;
    if (my < -20000 || my > 20000) return false;
    if (mz < -20000 || mz > 20000) return false;

    return true;
}

void ak09940_convert_to_uT(int32_t mx_raw, int32_t my_raw, int32_t mz_raw,
                           float *mx_uT, float *my_uT, float *mz_uT)
{
    if (mx_uT) *mx_uT = mx_raw * AK09940_SENSITIVITY_UT_PER_LSB;
    if (my_uT) *my_uT = my_raw * AK09940_SENSITIVITY_UT_PER_LSB;
    if (mz_uT) *mz_uT = mz_raw * AK09940_SENSITIVITY_UT_PER_LSB;
}

void ak09940_convert_to_nT(int32_t mx_raw, int32_t my_raw, int32_t mz_raw,
                           int32_t *mx_nT, int32_t *my_nT, int32_t *mz_nT)
{
    if (mx_nT) *mx_nT = mx_raw * AK09940_SENSITIVITY_NT_PER_LSB;
    if (my_nT) *my_nT = my_raw * AK09940_SENSITIVITY_NT_PER_LSB;
    if (mz_nT) *mz_nT = mz_raw * AK09940_SENSITIVITY_NT_PER_LSB;
}

void ak09940_convert_to_q16(int32_t mx_raw, int32_t my_raw, int32_t mz_raw,
                            int32_t *mx_q16, int32_t *my_q16, int32_t *mz_q16)
{
    if (mx_q16) *mx_q16 = mx_raw * AK09940_SENSITIVITY_Q16_PER_LSB;
    if (my_q16) *my_q16 = my_raw * AK09940_SENSITIVITY_Q16_PER_LSB;
    if (mz_q16) *mz_q16 = mz_raw * AK09940_SENSITIVITY_Q16_PER_LSB;
}
