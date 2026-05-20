#ifndef TAG_CORE_TIMEKEEPING_H
#define TAG_CORE_TIMEKEEPING_H

#include <stdbool.h>
#include <stdint.h>

int32_t GetTimeUnixSec(uint32_t *millis);
int SetTimeUnixSec(int32_t unix_time);
void stopMilliseconds(bool spiEnabled, unsigned int interval);

#endif
