/**
 * @file lps22hh.h
 * @brief Public API for descriptor-backed ST LPS22HH pressure sensors.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef LPS22HH_H
#define LPS22HH_H

#include "lps.h"

#include <stdint.h>
#include <stdbool.h>

/** @name LPS22HH configuration types
 * Output data rate selection.
 *
 * The enum values are chosen to map directly into the ODR field encoding used
 * by CTRL_REG1. Value 0 is power-down / one-shot arming state.
 * @{
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
/** @} */

/** @name LPS22HH conversion helpers
 * Convert raw pressure/temperature readings into engineering units.
 * @{
 */
/**
 * @brief Convert raw pressure to hectopascals.
 *
 * @param[in] pressure Raw signed pressure sample.
 * @return Pressure in hPa.
 */
float lps22hh_raw_pressure_hPA(int32_t pressure);

/**
 * @brief Convert raw temperature to degrees Celsius.
 *
 * @param[in] temperature Raw signed temperature sample.
 * @return Temperature in degrees Celsius.
 */
float lps22hh_raw_temperature_c(int32_t temperature);
/** @} */

/** @name Descriptor-backed LPS22HH API
 * Parameterized API. Tags provide their own pressure-device descriptor from
 * devices.c and call these functions with that descriptor.
 * @{
 */
/**
 * @brief Check the LPS22HH WHO_AM_I register.
 *
 * @param[in] device Pressure device descriptor.
 * @return true when the expected identity is present.
 */
bool lps22hh_check_who_am_i_device(const TagPressureDevice *device);

/**
 * @brief Put the LPS22HH into low-power idle mode.
 *
 * @param[in] device Pressure device descriptor.
 * @return 0 on success.
 */
int  lps22hh_set_idle_device(const TagPressureDevice *device);

/**
 * @brief Configure periodic pressure sampling.
 *
 * @param[in] device Pressure device descriptor.
 * @param[in] odr Output data rate.
 * @param[in] lpf Low-pass filter selection.
 * @return 0 on success.
 */
int  lps22hh_config_continuous_device(const TagPressureDevice *device,
                                      lps22hh_odr_t odr, lps22hh_lpf_t lpf);
/**
 * @brief Configure one-shot pressure sampling.
 *
 * @param[in] device Pressure device descriptor.
 * @param[in] lpf Low-pass filter selection.
 * @return 0 on success.
 */
int  lps22hh_config_triggered_device(const TagPressureDevice *device,
                                     lps22hh_lpf_t lpf);
/**
 * @brief Start one pressure conversion in one-shot mode.
 *
 * @param[in] device Pressure device descriptor.
 * @return 0 on success.
 */
int  lps22hh_trigger_one_shot_device(const TagPressureDevice *device);
/**
 * @brief Check whether a pressure sample is ready.
 *
 * @param[in] device Pressure device descriptor.
 * @return true when pressure data is available.
 */
bool lps22hh_data_ready_device(const TagPressureDevice *device);
/**
 * @brief Read raw pressure and temperature registers.
 *
 * @param[in] device Pressure device descriptor.
 * @param[out] pressure Raw signed pressure value.
 * @param[out] temperature Raw signed temperature value.
 * @return 0 on success.
 */
int  lps22hh_read_raw_device(const TagPressureDevice *device, int32_t *pressure,
                             int32_t *temperature);
/** @} */

#endif
