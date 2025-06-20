#ifndef DATALOG_H
#define DATALOG_H

// Stored Data Log -- in external memory

#define DATALOG_SAMPLES 60
typedef struct {
  struct {
    int16_t pressure;
    int16_t temperature;
  } data[DATALOG_SAMPLES];
} t_DataLog;

typedef struct {
  int32_t epoch;
  uint16_t vdd100[2];
} t_DataHeader;

extern t_DataHeader vddHeader[];

extern enum LOGERR writeDataLog(uint16_t *data, int num);
extern enum LOGERR writeDataHeader(t_DataHeader *head);
extern int restoreLog(void);

#endif