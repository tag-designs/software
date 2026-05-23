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

#include <tag.pb.h>
#include "core_state.h"

/** @brief Raw accelerometer and magnetometer sample stored in the data log. */
typedef struct {
    int16_t ax, ay, az, mx, my, mz;
} RawSensorData;

/** @brief Read one raw accelerometer/magnetometer sample pair. */
bool sensorSample(RawSensorData *data);
/** @brief State-machine handler for live calibration mode. */
enum Sleep Calibrating(enum StateTrans t, State_Event reason);
/** @brief Populate a monitor ACK with one live calibration sample. */
int calibration_logAck(Ack *ack);
/** @brief Persist calibration constants from the host. */
int write_calibration(CalibrationConstants *constants);
/** @brief Read stored calibration constants for the host. */
int read_calibration(int32_t index, Ack *ack);


#endif
