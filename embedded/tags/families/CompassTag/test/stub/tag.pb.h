/* Minimal stand-ins for the nanopb types the CompassTag sources use. The
   CompassTagLog layout mirrors embedded/proto-c/compasstag-proto-c. */
#ifndef STUB_TAG_PB_H
#define STUB_TAG_PB_H
#include <stdint.h>
typedef enum { RUNNING = 4, HIBERNATING = 5, FINISHED = 7 } TagState;
#define TagState_RUNNING RUNNING
#define TagState_FINISHED FINISHED
typedef int State_Event;
#define State_EVENT_ENDTIM 1
#define State_EVENT_INTERNALFULL 2
#define State_EVENT_EXTERNALFULL 3
#define State_EVENT_STARTHIB 4
typedef struct { int32_t start_epoch; int32_t end_epoch; } Config_Interval;
typedef int TestResult;
typedef struct { int unused; } CalibrationConstants;
typedef struct {
  float activity, ax, ay, az, mx, my, mz;
} CompassTagLog_Compass;
typedef struct {
  int32_t epoch;
  float voltage;
  float temperature;
  uint16_t data_count;
  CompassTagLog_Compass data[40];
  int32_t sample_period_s;
} CompassTagLog;
typedef enum { Ack_Err_OK = 0 } Ack_Err;
#define Ack_compasstag_data_log_tag 11
typedef struct {
  Ack_Err err;
  uint16_t which_payload;
  union { CompassTagLog compasstag_data_log; } payload;
} Ack;
#endif
