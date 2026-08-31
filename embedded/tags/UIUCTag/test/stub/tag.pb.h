/* Minimal stand-ins for the nanopb types state_run.c uses. */
#ifndef STUB_TAG_PB_H
#define STUB_TAG_PB_H
#include <stdint.h>
typedef enum { TagState_RUNNING = 5 } TagState;
typedef int State_Event;
typedef struct { int32_t start_epoch; int32_t end_epoch; } Config_Interval;
typedef int TestResult;
#define State_EVENT_ENDTIM 1
#define State_EVENT_INTERNALFULL 2
#define State_EVENT_STARTHIB 3
#define State_EVENT_EXCEPTION 4
#endif
