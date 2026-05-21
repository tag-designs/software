#ifndef LPS27HHW_H
#define LPS27HHW_H

#include "lps.h"

#include <stdbool.h>
#include <stdint.h>

bool lps27GetPressureTemp(const TagPressureDevice *device, int16_t *pressure,
                          int16_t *temperature);
bool lps27Test(const TagPressureDevice *device);
float lps27Pressure(int16_t pressure);
float lps27Temperature(int16_t temperature);

#endif
