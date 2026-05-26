/**
 * @file sensor_calibration.h
 * @brief Optional sensor calibration include shim.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_SENSOR_CALIBRATION_H
#define TAG_CORE_SENSOR_CALIBRATION_H

#include "custom.h"

#if defined(SENSOR_CALIBRATION) && SENSOR_CALIBRATION
#include "sensors.h"
#endif

#endif
