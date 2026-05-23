#ifndef _LPS_H_
#define _LPS_H_

#include "sensor_io.h"

#include <stdint.h>

/*
 * Pressure sensor instance descriptor.
 *
 * The register device describes both register access and the physical bus
 * session used for that access. Pressure drivers use tagPressureDeviceBegin()
 * and tagPressureDeviceEnd() to apply the standard power/bus sequence without
 * inferring whether the device is SPI, synchronous-USART, or I2C at runtime.
 */
typedef struct {
  const TagRegisterDevice *registers;
} TagPressureDevice;

void tagPressureDeviceBegin(const TagPressureDevice *device);
void tagPressureDeviceEnd(const TagPressureDevice *device);

#endif
