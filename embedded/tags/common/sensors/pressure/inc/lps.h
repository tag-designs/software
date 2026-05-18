#ifndef _LPS_H_
#define _LPS_H_

#include "sensor_io.h"

#include <stdbool.h>
#include <stdint.h>

typedef void (*TagPressurePower)(void);
typedef void (*TagPressureSleep)(int ms);

/*
 * Pressure sensor instance descriptor.
 *
 * The register device describes how the sensor is reached on the bus. The
 * power and sleep callbacks describe tag-specific power sequencing around that
 * bus transaction. Legacy pressure APIs use a module-local default descriptor;
 * new tag code can pass an explicit descriptor when the same driver is reused
 * with different board wiring or power policy.
 */
typedef struct {
  const TagRegisterDevice *registers;
  TagPressurePower power_on;
  TagPressurePower power_off;
  TagPressurePower bus_begin;
  TagPressurePower bus_end;
  TagPressureSleep sleep_ms;
} TagPressureDevice;

void tagPressureDeviceBegin(const TagPressureDevice *device);
void tagPressureDeviceEnd(const TagPressureDevice *device);

void lpsPowerOn(void);
void lpsPowerOff(void);
void lpsBusBegin(void);
void lpsBusEnd(void);
void lpsOn(void);
void lpsInit(void);
void lpsOff(void);
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
