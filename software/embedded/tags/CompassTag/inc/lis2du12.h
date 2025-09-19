#ifndef LIS2DU12_H
#define LIS2DU12_H

void accelInit(void);
void accelDeinit(void);
void accelReset(void);
bool accelTest(void);
bool accelSample(uint8_t *);


#endif