#ifndef TAG_CORE_SYNC_H
#define TAG_CORE_SYNC_H

#include <stdint.h>

#include "ch.h"

extern binary_semaphore_t ADCmutex;
extern binary_semaphore_t I2Cmutex;
extern binary_semaphore_t SPImutex;
extern int32_t timestamp;
extern uint32_t timestamp_millis;
extern thread_t *tpMain;

#endif
