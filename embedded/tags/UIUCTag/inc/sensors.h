/**
 * @file sensors.h
 * @brief UIUCTag collection-sensor configuration and sampling API.
 * @author tag firmware authors
 * @date 2026-08-31
 *
 * @details Keeps ADXL367 and BMP585 register-level detail out of the RUNNING
 *          state handler. state_run.c owns time, state, and log sequencing and
 *          reaches the sensors only through this interface, so a change of
 *          sampling strategy — notably the deferred move from polled DRDY to
 *          the LPS_RDY interrupt — stays inside sensors.c.
 *
 * @note    UIUCTag-local. The other BitPresTag family variants keep their
 *          sensor calls inside the shared family state_run.c.
 *
 * @see     ../../families/BitPresTag/design/uiuctag-data-collection.md
 */

#ifndef UIUCTAG_SENSORS_H
#define UIUCTAG_SENSORS_H

#include <stdbool.h>

/**
 * @brief Configure the accelerometer for activity wakeups during acquisition.
 *
 * @details Places the ADXL367 in wake-mode operation with the stored
 *          activity/inactivity thresholds from @c sconfig, maps the AWAKE
 *          event to the INT2 line, and leaves the device measuring. The
 *          accelerometer then drives the tag's activity wake source for the
 *          whole RUNNING state.
 *
 * @pre     Stored configuration has been loaded into @c sconfig.
 * @post    The ADXL367 is measuring in wake mode and its INT2 line reports
 *          AWAKE transitions; the bus session is closed.
 *
 * @note    The underlying register writes report no status, so there is no
 *          success value to return. A missing or unresponsive accelerometer is
 *          detected by the RUN_ADXL362 device test, not here.
 */
void initDataCollection(void);

/**
 * @brief Take one forced-mode pressure and temperature sample.
 *
 * @details Configures the BMP585 for a forced conversion, polls DRDY for the
 *          result, and powers the sensor back down before returning. Both
 *          outputs are always written: on any failure they are set to a quiet
 *          NaN so a caller can store the record unconditionally and the host
 *          sees "no measurement" rather than a stale or zero value.
 *
 * @param[out] pressure_hpa Pressure in hectopascals, or NaN when no sample was
 *                          captured.
 * @param[out] temperature_c BMP585 die temperature in degrees Celsius, or NaN
 *                           when no sample was captured.
 * @return true when a fresh sample was captured, false on configuration
 *         failure, DRDY timeout, or bus error.
 *
 * @post    The pressure rail is powered down and the bus session is closed on
 *          every path, including failures.
 *
 * @warning Blocks for the conversion time of the configured ODR while polling
 *          INT_STATUS. Not for use from an ISR.
 */
bool samplePressure(float *pressure_hpa, float *temperature_c);

#endif /* UIUCTAG_SENSORS_H */
