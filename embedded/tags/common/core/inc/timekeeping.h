#ifndef TAG_CORE_TIMEKEEPING_H
#define TAG_CORE_TIMEKEEPING_H

#include <stdint.h>

int32_t GetTimeUnixSec(uint32_t *millis);
int SetTimeUnixSec(int32_t unix_time);
void stopMilliseconds(unsigned int interval);

#endif
