/*
 * lps22xx.h
 *
 * Public interface for the ST LPS22HH and LPS22DF pressure sensor drivers.
 *
 * This header is paired with lps22xx.c and contains:
 *   - register definitions
 *   - bit masks
 *   - enums for mode selection
 *   - function prototypes
 *
 * Assumptions:
 *   - The platform provides spi_write() and spi_read().
 *   - The SPI layer handles chip select and bus timing.
 *   - The sensor SPI protocol supports auto-increment reads when the
 *     register address MSB is set.
 */

#ifndef LPS22HH_H
#define LPS22HH_H

#include "lps.h"

#include <stdint.h>
#include <stdbool.h>

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

float lps22hh_raw_pressure_hPA(int32_t pressure);
float lps22hh_raw_temperature_c(int32_t temperature);

/*
 * Parameterized API. Tags provide their own pressure-device descriptor from
 * devices.c and call these functions with that descriptor.
 */
bool lps22hh_check_who_am_i_device(const TagPressureDevice *device);
int  lps22hh_set_idle_device(const TagPressureDevice *device);
int  lps22hh_config_continuous_device(const TagPressureDevice *device,
                                      lps22hh_odr_t odr, lps22hh_lpf_t lpf);
int  lps22hh_config_triggered_device(const TagPressureDevice *device,
                                     lps22hh_lpf_t lpf);
int  lps22hh_trigger_one_shot_device(const TagPressureDevice *device);
bool lps22hh_data_ready_device(const TagPressureDevice *device);
int  lps22hh_read_raw_device(const TagPressureDevice *device, int32_t *pressure,
                             int32_t *temperature);

#endif
