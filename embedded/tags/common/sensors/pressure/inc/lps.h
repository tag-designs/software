/**
 * @file lps.h
 * @brief Shared pressure-sensor descriptor and lifecycle helpers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef _LPS_H_
#define _LPS_H_

#include "sensor_io.h"

#include <stdint.h>

/** @name Pressure sensor descriptor
 * Pressure sensor instance descriptor.
 *
 * The register device describes both register access and the physical bus
 * session used for that access. Pressure drivers use tagPressureDeviceBegin()
 * and tagPressureDeviceEnd() to apply the standard power/bus sequence without
 * inferring whether the device is SPI, synchronous-USART, or I2C at runtime.
 * @{
 */
typedef struct {
  const TagRegisterDevice *registers;
} TagPressureDevice;
/** @} */

/** @name Pressure device lifecycle
 * Helpers bracket pressure register access with the standard bus power/session
 * sequence.
 * @{
 */
/**
 * @brief Power and begin the bus session for a pressure device.
 *
 * @param[in] device Pressure device descriptor.
 */
void tagPressureDeviceBegin(const TagPressureDevice *device);

/**
 * @brief End the bus session and power down a pressure device.
 *
 * @param[in] device Pressure device descriptor.
 */
void tagPressureDeviceEnd(const TagPressureDevice *device);
/** @} */

#endif
