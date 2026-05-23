/**
 * @file lps27hhw.h
 * @brief Public API for descriptor-backed ST LPS27HHW pressure sensors.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef LPS27HHW_H
#define LPS27HHW_H

#include "lps.h"

#include <stdbool.h>
#include <stdint.h>

/** @name LPS27HHW pressure API
 * Descriptor-backed helpers for one-shot pressure/temperature reads and test.
 * @{
 */
/**
 * @brief Trigger and read one pressure/temperature sample.
 *
 * @param[in] device Pressure device descriptor.
 * @param[out] pressure Raw pressure output.
 * @param[out] temperature Raw temperature output.
 * @return true when a fresh sample was read.
 */
bool lps27GetPressureTemp(const TagPressureDevice *device, int16_t *pressure,
                          int16_t *temperature);

/**
 * @brief Verify identity and sample readiness for an LPS27HHW device.
 *
 * @param[in] device Pressure device descriptor.
 * @return true when identity and sample read pass.
 */
bool lps27Test(const TagPressureDevice *device);

/**
 * @brief Convert raw pressure to hectopascals.
 *
 * @param[in] pressure Raw pressure output.
 * @return Pressure in hPa.
 */
float lps27Pressure(int16_t pressure);

/**
 * @brief Convert raw temperature to degrees Celsius.
 *
 * @param[in] temperature Raw temperature output.
 * @return Temperature in degrees Celsius.
 */
float lps27Temperature(int16_t temperature);
/** @} */

#endif
