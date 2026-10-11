/**
 * @file sensors.h
 * @brief CompassTag sensor sampling and calibration API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef _SENSORS_H
#define _SENSORS_H

#include <stdbool.h>
#include <stdint.h>

#include <tag.pb.h>
#include "core_state.h"

/**
 * @brief Raw accelerometer and magnetometer sample stored in the data log.
 *
 * @details Counts, not engineering units; datalog.c converts on download.
 *          - ax, ay, az: 12-bit LIS2DU12 counts at +-2 g, 0.976 mg each
 *            (ACCEL_CONSTANT). The sensor left-justifies 12 bits in a 16-bit
 *            register pair whose low 4 bits always read zero, so the division
 *            by 16 in sensors.c is exact and adds no rounding.
 *          - mx, my, mz: the top 16 bits of the 18-bit AK09940A reading,
 *            0.04 uT each. This is four times MAG_CONSTANT, which describes
 *            the 18-bit calibration stream instead.
 *
 * @see sensorSample(), sensorCalibrationSample()
 */
typedef struct {
    int16_t ax, ay, az, mx, my, mz;
} RawSensorData;

/**
 * @brief Read one raw accelerometer/magnetometer sample pair.
 *
 * @param[out] data Raw sample destination.
 * @return true when all enabled sensors sampled successfully.
 */
bool sensorSample(RawSensorData *data);
/**
 * @brief State-machine handler for live calibration mode.
 *
 * @param[in] t State transition kind.
 * @param[in] reason Reason for entering the state.
 * @return Requested low-power mode after handling calibration.
 */
enum Sleep Calibrating(enum StateTrans t, State_Event reason);
/**
 * @brief Populate a monitor ACK with one live calibration sample.
 *
 * @param[out] ack ACK message to populate.
 * @return Encoded ACK length.
 */
int calibration_logAck(Ack *ack);
/**
 * @brief Persist calibration constants from the host.
 *
 * @param[in] constants Calibration constants message.
 * @return Encoded ACK length or error response length.
 */
int write_calibration(CalibrationConstants *constants);
/**
 * @brief Read stored calibration constants for the host.
 *
 * @param[in] index Calibration slot to read, or negative for latest.
 * @param[out] ack ACK message to populate.
 * @return Encoded ACK length or error response length.
 */
int read_calibration(int32_t index, Ack *ack);

/**
 * @brief Report whether at least one calibration entry is stored.
 *
 * @return true when data collection has calibration constants available.
 */
bool sensorsHaveCalibration(void);


#endif
