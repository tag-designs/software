#ifndef LIS2DU12_H
#define LIS2DU12_H

typedef enum {ACCEL_WAKEUP_MODE, 
            ACCEL_SAMPLE_50HZ_MODE, 
            ACCEL_SAMPLE_100HZ_MODE} lis2du12mode_t;

void accelInit(lis2du12mode_t mode);
void accelDeinit(void);
void accelReset(void);
bool accelTest(void);
bool accelSample(uint8_t *);


#endif