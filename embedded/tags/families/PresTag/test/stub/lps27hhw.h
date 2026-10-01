#ifndef STUB_LPS27HHW_H
#define STUB_LPS27HHW_H
#include <stdint.h>
#include "devices.h"
void lps27GetPressureTemp(const TagRegisterDevice *dev, int16_t *pressure, int16_t *temperature);
float lps27Pressure(int16_t pressure);
float lps27Temperature(int16_t temperature);
#endif
