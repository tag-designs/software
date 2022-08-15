#ifndef _LIS2DTW12_H
#define _LIS2DTW12_H

void lis2_sample(int samples, int16_t *rms, int16_t orientation[3]);
bool lis2_test(void);

#endif