#ifndef DATALOG_H
#define DATALOG_H
#include "sensors.h"

// Stored Data Log -- in external memory

#define DATALOG_SAMPLES 10

// sensors recorded every 15 seconds, activity every minute

typedef struct {
  struct { 
   RawSensorData sensors[4];
    uint16_t activity;
  } data[DATALOG_SAMPLES];
} t_DataLog;

typedef struct {
  int32_t epoch;
  uint16_t vdd100;
  uint16_t temp10;
} t_DataHeader;

extern t_DataHeader vddHeader[];

extern enum LOGERR writeDataLog(uint16_t *data, int num);
extern enum LOGERR writeDataHeader(t_DataHeader *head);
extern int restoreLog(void);

#endif