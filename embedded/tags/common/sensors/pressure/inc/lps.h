#ifndef _LPS_H_
#define _LPS_H_

void lpsOn(void);
void lpsInit(void);
void lpsOff(void);
bool lpsGetPressureTemp(int16_t *pressure, int16_t *temperature);
bool lpsTest(void);
float lpsPressure(int16_t pressure);
float lpsTemperature(int16_t temperature);
#endif