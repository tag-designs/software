#ifndef DATALOG_H
#define DATALOG_H

// Stored Data Log -- in external memory

#define DATALOG_SAMPLES 30
typedef struct {
  int32_t epoch;
  int16_t temp10;
  uint16_t vdd100;
  struct {
    int16_t pressure;
    int16_t temperature;
  } data[DATALOG_SAMPLES];
} t_DataLog;

extern enum LOGERR writeDataLog(uint16_t *data, int num);
extern int restoreLog(void);

#endif