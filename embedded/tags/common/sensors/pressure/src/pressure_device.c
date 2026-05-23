/**
 * @file pressure_device.c
 * @brief Shared pressure-sensor power and bus lifecycle helpers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "lps.h"

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
void tagPressureDeviceBegin(const TagPressureDevice *device)
{
  tagBusPowerOn(&device->registers->bus);
  tagBusBegin(&device->registers->bus);
}

/**
 * @brief End the bus session and power down a pressure device.
 *
 * @param[in] device Pressure device descriptor.
 */
void tagPressureDeviceEnd(const TagPressureDevice *device)
{
  tagBusEnd(&device->registers->bus);
  tagBusPowerOff(&device->registers->bus);
}
/** @} */
