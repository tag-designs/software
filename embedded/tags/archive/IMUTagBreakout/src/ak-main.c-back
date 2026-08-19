/**
 * @file main.c
 * @brief Minimal ChibiOS example for the AK09940A driver.
 *
 * This example shows:
 * - ChibiOS initialization
 * - SPI startup
 * - Driver object initialization
 * - WHO_AM_I check
 * - Continuous-mode setup
 * - Interrupt-style DRDY handling
 * - Raw sample read plus engineering-unit conversion
 *
 * The code intentionally keeps the application logic simple so it can be used
 * as a starting point for a real board port.
 */

#include "ch.h"
#include "hal.h"
#include "ak09940.h"

static ak09940_t mag;

/* Example SPI configuration.
 * Replace GPIOA/4 and the clock prescaler with values suitable for your board.
 */
static const SPIConfig spicfg = {
    .ssport = GPIOA,
    .sspad  = 4,
    .cr1    = SPI_CR1_BR_2 | SPI_CR1_BR_1,
    .cr2    = 0
};

/* Small callback used by the DRDY ISR wrapper.
 * In a real application this would usually just wake a thread or set an event.
 */
static void mag_drdy_cb(void *arg)
{
    ak09940_handle_drdy_isr((ak09940_t *)arg);
}

/* Example helper showing how a trigger pulse would be generated in triggered mode.
 * This is not used in the continuous-mode example below.
 */
static void pulse_trigger(ak09940_t *dev)
{
    palSetLine(dev->drdy_trg_line);
    chThdSleepMicroseconds(5);
    palClearLine(dev->drdy_trg_line);
    (void)dev;
}

int main(void)
{
    halInit();
    chSysInit();

    /* Start the SPI peripheral used by the sensor. */
    spiStart(&SPID1, &spicfg);

    /* Bind the driver object to the chosen SPI bus and DRDY/TRG line. */
    ak09940ObjectInit(&mag, &SPID1, &spicfg, PAL_LINE(GPIOC, 13));
    ak09940SetDrdyCallback(&mag, mag_drdy_cb, &mag);

    /* Confirm that the device responds with the expected identity value. */
    if (!ak09940_check_whoami(&mag)) {
        while (true) {
            chThdSleepMilliseconds(1000);
        }
    }

    /* Continuous mode example:
     * - 100 Hz sample rate
     * - low-noise drive 2
     * - temperature enabled
     */
    if (ak09940_init_continuous(&mag,
                                AK09940_RATE_100HZ,
                                AK09940_DRIVE_LOW_NOISE_2,
                                AK09940_TEMP_ENABLED) != MSG_OK) {
        while (true) {
            chThdSleepMilliseconds(1000);
        }
    }

    /* Enable interrupt-style handling of the shared DRDY/TRG line. */
    ak09940_enable_drdy_interrupt(&mag, true);

    while (true) {
        if (ak09940_data_ready(&mag)) {
            int32_t mx, my, mz;
            int16_t temp;

            /* Read one complete coherent sample. */
            if (ak09940_read_sample(&mag, &mx, &my, &mz, &temp)) {
                float fx, fy, fz;
                int32_t nx, ny, nz;

                /* Convert raw counts to engineering units. */
                ak09940_convert_to_uT(mx, my, mz, &fx, &fy, &fz);
                ak09940_convert_to_nT(mx, my, mz, &nx, &ny, &nz);

                /*
                 * At this point you could:
                 * - feed fx/fy/fz into a fusion filter
                 * - stream the values over UART
                 * - store them in a ring buffer
                 */
                (void)fx;
                (void)fy;
                (void)fz;
                (void)nx;
                (void)ny;
                (void)nz;
                (void)temp;
            }
        }

        /* Keep the loop polite. In an RTOS app you would likely wait on an event. */
        chThdSleepMilliseconds(1);
    }

    return 0;
}
