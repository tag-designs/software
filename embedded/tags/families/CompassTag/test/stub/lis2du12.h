#ifndef STUB_LIS2DU12_H
#define STUB_LIS2DU12_H
#include "devices.h"
typedef enum { ACCEL_WAKEUP_MODE } lis2du12mode_t;
void lis2du12Init(const TagRegisterDevice *device, lis2du12mode_t mode);
void lis2du12Deinit(const TagRegisterDevice *device);
#endif
