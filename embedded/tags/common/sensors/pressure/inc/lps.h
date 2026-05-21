#ifndef _LPS_H_
#define _LPS_H_

#include "sensor_io.h"

#include <stdbool.h>
#include <stdint.h>

typedef void (*TagPressureSleep)(int ms);

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
  TagPressureSleep sleep_ms;
} TagPressureDevice;

void tagPressureDeviceBegin(const TagPressureDevice *device);
void tagPressureDeviceEnd(const TagPressureDevice *device);

void lpsInit(void);
bool lpsGetPressureTemp(int16_t *pressure, int16_t *temperature);
bool lpsTest(void);
float lpsPressure(int16_t pressure);
float lpsTemperature(int16_t temperature);

bool lps27GetPressureTemp(const TagPressureDevice *device, int16_t *pressure,
                          int16_t *temperature);
bool lps27Test(const TagPressureDevice *device);
float lps27Pressure(int16_t pressure);
float lps27Temperature(int16_t temperature);
bool lps22GetPressureTemp(const TagPressureDevice *device, int16_t *pressure,
                          int16_t *temperature);
bool lps22Test(const TagPressureDevice *device);
float lps22Pressure(int16_t pressure);
float lps22Temperature(int16_t temperature);
bool lps33GetPressureTemp(const TagPressureDevice *device, int16_t *pressure,
                          int16_t *temperature);
bool lps33Test(const TagPressureDevice *device);
float lps33Pressure(int16_t pressure);
float lps33Temperature(int16_t temperature);
bool bmp5GetPressureTemp(const TagPressureDevice *device, int16_t *pressure,
                         int16_t *temperature);
bool bmp5Test(const TagPressureDevice *device);
float bmp5Pressure(int16_t pressure);
float bmp5Temperature(int16_t temperature);
#endif
