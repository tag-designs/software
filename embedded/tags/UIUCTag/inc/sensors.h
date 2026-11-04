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
 * @see     ../design/data-collection.md
 */

#ifndef UIUCTAG_SENSORS_H
#define UIUCTAG_SENSORS_H

#include <stdbool.h>
#include <stdint.h>

#include "uiuctag_log_format.h"

/**
 * @brief Configure the accelerometer for activity wakeups during acquisition.
 *
 * @details Places the ADXL367 in wake-mode operation with the stored
 *          activity/inactivity thresholds from @c sconfig, maps the AWAKE
 *          event to the INT1 line (UIUCTag wires INT1, not INT2, to the MCU
 *          wakeup pin), and leaves the device measuring. The accelerometer
 *          then drives the tag's activity wake source for the whole RUNNING
 *          state.
 *
 * @pre     Stored configuration has been loaded into @c sconfig.
 * @post    The ADXL367 is measuring in wake mode and its INT1 line reports
 *          AWAKE transitions; the bus session is closed.
 *
 * @note    The underlying register writes report no status, so there is no
 *          success value to return. A missing or unresponsive accelerometer is
 *          detected by the RUN_ADXL362 device test, not here.
 */
void initDataCollection(void);

/**
 * @brief Take one forced-mode pressure and temperature reading, unconverted.
 *
 * @details Configures the BMP585 for a forced conversion, polls DRDY for the
 *          result, and powers the sensor back down before returning. Fills the
 *          record's pressure and temperature fields with the sensor's data
 *          registers as they read, and leaves its activity word alone. A
 *          failed conversion, a DRDY timeout, a bus error, or a pressure
 *          outside UIUCTAG_PRESSURE_RAW_MIN..MAX stores UIUCTAG_RAW_FAILED in
 *          both fields, so the caller can store the record unconditionally.
 *
 * @param[out] record Record whose pressure and temperature are filled in.
 * @return true when a reading was captured, false when @p record holds the
 *         failure value.
 *
 * @post    The pressure rail is powered down and the bus session is closed on
 *          every path, including failures.
 *
 * @warning Blocks for the conversion time of the configured ODR while polling
 *          INT_STATUS. Not for use from an ISR.
 */
bool samplePressureRaw(t_UIUCTagRecord *record);

#endif /* UIUCTAG_SENSORS_H */
