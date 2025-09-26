#ifndef LIS2DU12_H
#define LIS2DU12_H

typedef enum {WAKEUP, SAMPLE_50HZ, SAMPLE_100HZ} lis2du12mode_t;

void accelInit(lis2du12mode_t mode);
void accelDeinit(void);
void accelReset(void);
bool accelTest(void);
bool accelSample(uint8_t *);


#endif