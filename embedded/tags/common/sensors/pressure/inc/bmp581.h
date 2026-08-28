/**
 * @file bmp581.h
 * @brief Descriptor-backed Bosch BMP581 pressure sensor API.
 * @author tag firmware authors
 * @date 2026-08-28
 */

#ifndef TAG_BMP581_H
#define TAG_BMP581_H

#include "bmp5_defs.h"
#include "lps.h"

#include <stdbool.h>
#include <stdint.h>

/** @name BMP581 configuration types
 * Output data-rate choices used by IMUTag collection.
 *
 * The enum values intentionally mirror the Bosch BMP5 SensorAPI ODR encodings
 * so callers can pass them directly into the vendor configuration structure.
 * @{
 */
typedef enum {
  BMP581_ODR_10HZ = BMP5_ODR_10_HZ,
  BMP581_ODR_25HZ = BMP5_ODR_25_HZ,
  BMP581_ODR_50HZ = BMP5_ODR_50_HZ,
  BMP581_ODR_100HZ = BMP5_ODR_100_2_HZ,
  BMP581_ODR_200HZ = BMP5_ODR_199_1_HZ
} bmp581_odr_t;
/** @} */

/** @name Descriptor-backed BMP581 API
 * Parameterized API. Tags provide their own pressure-device descriptor from
 * devices.c and call these functions with that descriptor.
 * @{
 */
/**
 * @brief Check the BMP581 chip-id register through the configured register bus.
 *
 * @param[in] device Pressure device descriptor.
 * @return true when the Bosch primary or secondary BMP5 chip-id is present.
 */
bool bmp581_check_who_am_i_device(const TagPressureDevice *device);

/**
 * @brief Put the BMP581 into standby mode.
 *
 * @param[in] device Pressure device descriptor.
 * @return 0 on success or a negative Bosch SensorAPI error.
 */
int bmp581_set_idle_device(const TagPressureDevice *device);

/**
 * @brief Configure periodic pressure and temperature sampling.
 *
 * @param[in] device Pressure device descriptor.
 * @param[in] odr Output data rate for the continuous conversion stream.
 * @return 0 on success or a negative Bosch SensorAPI error.
 */
int bmp581_config_continuous_device(const TagPressureDevice *device,
                                    bmp581_odr_t odr);

/**
 * @brief Check the BMP581 data-ready interrupt status.
 *
 * @param[in] device Pressure device descriptor.
 * @return true when a pressure/temperature sample is available.
 */
bool bmp581_data_ready_device(const TagPressureDevice *device);

/**
 * @brief Read one compensated pressure and temperature sample.
 *
 * @param[in] device Pressure device descriptor.
 * @param[out] pressure_hpa Pressure in hectopascals.
 * @param[out] temperature_centi_c Temperature in centi-degrees Celsius.
 * @return 0 on success or a negative Bosch SensorAPI error.
 */
int bmp581_read_pressure_temp_device(const TagPressureDevice *device,
                                     float *pressure_hpa,
                                     int16_t *temperature_centi_c);
/** @} */

#endif
