/**
 * @file sensors.h
 * @brief IMUTag family sensor sampling and calibration API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef _SENSORS_H
#define _SENSORS_H

#include <stdbool.h>
#include <stdint.h>

#include "imutag_log_format.h"
#include <tag.pb.h>
#include "core_state.h"

/** One raw synchronized IMU sample pair stored in the data log. */
typedef t_ImuTagRawSensorData RawSensorData;

/**
 * @brief Clear one raw sensor sample; retained for legacy RUNNING code.
 *
 * @param[out] data Raw sample record to clear.
 * @return true after the record has been initialized.
 */
bool sensorSample(RawSensorData *data);
/**
 * @brief Configure IMU, magnetometer, and pressure sensor for collection.
 *
 * @return true when all collection devices are configured and ready.
 */
bool initDataCollection(void);
/**
 * @brief Fill one superframe when the IMU FIFO has enough samples.
 *
 * @param[out] frame Superframe populated with IMU and auxiliary samples.
 * @return true when a complete superframe was captured.
 */
bool sampleDataCollection(t_ImuTagSuperFrame *frame);
/**
 * @brief Return the latest raw LPS22HH temperature in hundredths of a degree C.
 *
 * @param[out] rawtemp Latest pressure-sensor temperature sample.
 * @return true when a pressure sample has been captured since collection start.
 */
bool latestDataCollectionRawTemp(int16_t *rawtemp);
/**
 * @brief Shut down collection sensors and trigger generation.
 *
 * @return true when devices were returned to a low-power state.
 */
bool deinitDataCollection(void);
/**
 * @brief State-machine handler for live calibration mode.
 *
 * @param[in] t State transition phase.
 * @param[in] reason Event that caused this calibration-state action.
 * @return Requested sleep mode after calibration handling.
 */
enum Sleep Calibrating(enum StateTrans t, State_Event reason);
/**
 * @brief Populate a monitor ACK with one live calibration sample.
 *
 * @param[out] ack Monitor acknowledgement to populate.
 * @return 0 on success or a negative error when no sample is available.
 */
int calibration_logAck(Ack *ack);
/**
 * @brief Persist calibration constants from the host.
 *
 * @param[in] constants Calibration constants supplied by the host.
 * @return 0 on success or a negative storage/validation error.
 */
int write_calibration(CalibrationConstants *constants);
/**
 * @brief Read stored calibration constants for the host.
 *
 * @param[in] index Calibration record index requested by the host.
 * @param[out] ack Monitor acknowledgement to populate.
 * @return 0 on success or a negative error when the record is unavailable.
 */
int read_calibration(int32_t index, Ack *ack);
/**
 * @brief Report whether at least one calibration entry is stored.
 *
 * @return true when stored calibration data is available.
 */
bool sensorsHaveCalibration(void);


#endif
