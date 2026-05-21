#ifndef LIS2DU12_H
#define LIS2DU12_H

#include "sensor_io.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {ACCEL_WAKEUP_MODE, 
            ACCEL_SAMPLE_50HZ_MODE, 
            ACCEL_SAMPLE_100HZ_MODE} lis2du12mode_t;

/*
 * LIS2DU12 is a CompassTag-family register driver. Its public API takes the
 * configured register device directly; the tag devices.c file owns the
 * physical bus wiring, controller setup, and chip-select transaction details.
 */
void lis2du12Init(const TagRegisterDevice *device, lis2du12mode_t mode);
void lis2du12Deinit(const TagRegisterDevice *device);
void lis2du12Reset(const TagRegisterDevice *device);
bool lis2du12Test(const TagRegisterDevice *device);
bool lis2du12Sample(const TagRegisterDevice *device, uint8_t *data);
const TagRegisterDevice *tagLis2du12Device(void);

void accelInit(lis2du12mode_t mode);
void accelDeinit(void);
void accelReset(void);
bool accelTest(void);
bool accelSample(uint8_t *);


#endif
