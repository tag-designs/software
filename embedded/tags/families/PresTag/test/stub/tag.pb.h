/* Minimal stand-ins for the nanopb types the PresTag sources use. PresTagLog
   mirrors embedded/proto-c/prestag-proto-c, PresTagRawLog prestagraw-proto-c
   (samples max_size 3780, default-options/tagdata.options). */
#ifndef STUB_TAG_PB_H
#define STUB_TAG_PB_H
#include <stdint.h>
typedef enum { RUNNING = 4, HIBERNATING = 5, FINISHED = 7 } TagState;
#define TagState_RUNNING RUNNING
typedef int State_Event;
#define State_EVENT_ENDTIM 1
#define State_EVENT_INTERNALFULL 2
#define State_EVENT_EXTERNALFULL 3
#define State_EVENT_STARTHIB 4
#define State_EVENT_POWERFAIL 13
typedef struct { int32_t start_epoch; int32_t end_epoch; } Config_Interval;
typedef int TestResult;
typedef struct { float temperature, pressure; } PresTagLog_PT;
typedef struct {
  int32_t epoch;
  float voltage;
  float temperature;
  uint16_t data_count;
  PresTagLog_PT data[60];
} PresTagLog;
typedef struct {
  float temp_constant;
  float pres_constant;
  struct { uint16_t size; uint8_t bytes[3780]; } samples;
} PresTagRawLog;
typedef enum { Ack_Err_OK = 0 } Ack_Err;
#define Ack_prestag_data_log_tag 8
#define Ack_prestag_raw_data_log_tag 18
typedef struct {
  Ack_Err err;
  uint16_t which_payload;
  union { PresTagLog prestag_data_log; PresTagRawLog prestag_raw_data_log; } payload;
} Ack;
#endif
