#ifndef STUB_LPS27HHW_H
#define STUB_LPS27HHW_H
#include <stdbool.h>
#include <stdint.h>
#include "devices.h"
#define LPS27_FULL_SAMPLE_BYTES 5
void lps27GetPressureTemp(const TagRegisterDevice *dev, int16_t *pressure, int16_t *temperature);
bool lps27GetPressureTempFull(const TagRegisterDevice *dev, bool low_noise,
                              uint8_t raw[LPS27_FULL_SAMPLE_BYTES]);
float lps27Pressure(int16_t pressure);
float lps27Temperature(int16_t temperature);
#endif
