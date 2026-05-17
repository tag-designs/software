#ifndef _SENSORS_H
#define _SENSORS_H

#include <stdbool.h>
#include <stdint.h>

#include <tag.pb.h>
#include "core_state.h"

typedef struct {
    int16_t ax, ay, az, mx, my, mz;
} RawSensorData;

bool sensorSample(RawSensorData *data);
enum Sleep Calibrating(enum StateTrans t, State_Event reason);
int calibration_logAck(Ack *ack);
int write_calibration(CalibrationConstants *constants);
int read_calibration(int32_t index, Ack *ack);


#endif
