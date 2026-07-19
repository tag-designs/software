/**
 * @file sensors.h
 * @brief IMUTagBreakout sensor sampling and calibration API.
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

/** @brief One raw synchronized IMU sample pair stored in the data log. */
typedef t_ImuTagRawSensorData RawSensorData;

/** @brief Clear one raw sensor sample; retained for legacy RUNNING code. */
bool sensorSample(RawSensorData *data);
/** @brief Configure IMU, magnetometer, and pressure sensor for collection. */
bool initDataCollection(void);
/** @brief Fill one superframe when the IMU FIFO has enough samples. */
bool sampleDataCollection(t_ImuTagSuperFrame *frame);
/** @brief Return the latest raw LPS22HH temperature in hundredths of a degree C. */
bool latestDataCollectionRawTemp(int16_t *rawtemp);
/** @brief Shut down collection sensors and trigger generation. */
bool deinitDataCollection(void);
/** @brief State-machine handler for live calibration mode. */
enum Sleep Calibrating(enum StateTrans t, State_Event reason);
/** @brief Populate a monitor ACK with one live calibration sample. */
int calibration_logAck(Ack *ack);
/** @brief Persist calibration constants from the host. */
int write_calibration(CalibrationConstants *constants);
/** @brief Read stored calibration constants for the host. */
int read_calibration(int32_t index, Ack *ack);
/** @brief Report whether at least one calibration entry is stored. */
bool sensorsHaveCalibration(void);


#endif
