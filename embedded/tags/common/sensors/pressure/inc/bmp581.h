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

/**
 * @def     BMP581_E_DATA_READY_TIMEOUT
 * @brief   Driver-local timeout returned when forced-mode DRDY never asserts.
 */
#define BMP581_E_DATA_READY_TIMEOUT INT8_C(-100)

/**
 * @struct  bmp581_interrupt_config_t
 * @brief   BMP581 interrupt pin mode used while enabling data-ready events.
 */
typedef struct {
  enum bmp5_intr_mode mode;     ///< Latched or pulsed interrupt behavior.
  enum bmp5_intr_polarity polarity; ///< Active level driven by the sensor.
  enum bmp5_intr_drive drive;   ///< Push-pull or open-drain output driver.
} bmp581_interrupt_config_t;

/**
 * @def     BMP581_INTERRUPT_PUSH_PULL_PULSED_ACTIVE_HIGH
 * @brief   Current IMUTag-style data-ready output mode.
 */
#define BMP581_INTERRUPT_PUSH_PULL_PULSED_ACTIVE_HIGH \
  { BMP5_PULSED, BMP5_ACTIVE_HIGH, BMP5_INTR_PUSH_PULL }

/**
 * @def     BMP581_INTERRUPT_OPEN_DRAIN_LATCHED_ACTIVE_LOW
 * @brief   Low-power wake mode for pull-up biased DRDY/POR interrupt lines.
 */
#define BMP581_INTERRUPT_OPEN_DRAIN_LATCHED_ACTIVE_LOW \
  { BMP5_LATCHED, BMP5_ACTIVE_LOW, BMP5_INTR_OPEN_DRAIN }

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
 * @brief Configure forced-mode pressure sampling and leave the BMP581 powered.
 *
 * @details Initializes the sensor, configures pressure/temperature conversion,
 *          enables the data-ready interrupt source, applies @p interrupt_config
 *          if supplied, and leaves the device in standby with its pressure rail
 *          powered. The bus session is closed before return so callers may
 *          enter STOP/STANDBY while waiting for a later interrupt.
 *
 * @param[in] device Pressure device descriptor.
 * @param[in] odr Output data rate encoding used by the BMP5 OSR/ODR register.
 * @param[in] interrupt_config Optional interrupt pin configuration. Passing
 *                             NULL selects latched, active-low open-drain mode.
 * @return 0 on success or a negative Bosch SensorAPI error.
 * @post On success the pressure rail remains on and the bus session is closed.
 */
int bmp581_config_forced_device(const TagPressureDevice *device,
                                bmp581_odr_t odr,
                                const bmp581_interrupt_config_t *interrupt_config);

/**
 * @brief Trigger one forced-mode conversion on a powered BMP581.
 *
 * @details Starts one pressure/temperature conversion by placing the sensor in
 *          forced mode. The pressure rail is left on and the bus session is
 *          closed before return so the caller can sleep until DRDY asserts.
 *
 * @param[in] device Pressure device descriptor already configured for forced
 *                   sampling.
 * @return 0 on success or a negative Bosch SensorAPI error.
 * @pre bmp581_config_forced_device() must have completed since the most recent
 *      sensor power cycle.
 */
int bmp581_trigger_forced_device(const TagPressureDevice *device);

/**
 * @brief Read and clear the BMP581 interrupt-status register while powered.
 *
 * @details Reading INT_STATUS clears latched interrupt causes. This helper
 *          intentionally leaves the pressure rail on so a state machine can
 *          decode POR/DRDY wake causes before reading the sample.
 *
 * @param[in] device Pressure device descriptor.
 * @param[out] int_status Raw BMP5_REG_INT_STATUS value.
 * @return 0 on success or a negative Bosch SensorAPI error.
 */
int bmp581_clear_interrupt_status_device(const TagPressureDevice *device,
                                         uint8_t *int_status);

/**
 * @brief Check the BMP581 data-ready interrupt status.
 *
 * @param[in] device Pressure device descriptor.
 * @return true when a pressure/temperature sample is available.
 */
bool bmp581_data_ready_device(const TagPressureDevice *device);

/**
 * @brief Read one compensated sample without powering down the BMP581.
 *
 * @details Use this after a forced-mode DRDY wake. The bus session is opened
 *          only for the register transfer; sensor power and interrupt state are
 *          left under the caller's control.
 *
 * @param[in] device Pressure device descriptor.
 * @param[out] pressure_hpa Pressure in hectopascals.
 * @param[out] temperature_centi_c Temperature in centi-degrees Celsius.
 * @return 0 on success or a negative Bosch SensorAPI error.
 * @pre A forced or continuous conversion result must be available.
 */
int bmp581_read_pressure_temp_powered_device(const TagPressureDevice *device,
                                             float *pressure_hpa,
                                             int16_t *temperature_centi_c);

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

/**
 * @brief Trigger and wait for one forced-mode BMP581 sample.
 *
 * @details Convenience helper for self-tests and fallback diagnostics. It
 *          assumes the sensor has already been configured for forced mode and
 *          remains powered. Production BitPresTag collection should normally
 *          sleep on the DRDY GPIO instead of polling with this helper.
 *
 * @param[in] device Pressure device descriptor.
 * @param[in] timeout_us Maximum time to poll INT_STATUS for DRDY.
 * @param[out] pressure_hpa Pressure in hectopascals.
 * @param[out] temperature_centi_c Temperature in centi-degrees Celsius.
 * @return 0 on success, BMP581_E_DATA_READY_TIMEOUT when DRDY does not assert,
 *         or a negative Bosch SensorAPI error.
 */
int bmp581_sample_forced_blocking_device(const TagPressureDevice *device,
                                         uint32_t timeout_us,
                                         float *pressure_hpa,
                                         int16_t *temperature_centi_c);
/** @} */

#endif
