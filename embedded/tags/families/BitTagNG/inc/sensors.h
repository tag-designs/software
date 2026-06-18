/**
 * @file sensors.h
 * @brief BitTagNG sensor initialization API.
 * @author tag firmware authors
 * @date 2026-06-16
 */

#ifndef BITTAGNG_SENSORS_H
#define BITTAGNG_SENSORS_H

#include <stdbool.h>

/** @brief Configure the ADXL367 activity sensor for BitTagNG run mode. */
bool initActivitySensor(void);

/** @brief Read ADXL367 activity state and arm the next transition interrupt. */
bool checkActivitySensorAwake(bool *is_awake);

#endif
