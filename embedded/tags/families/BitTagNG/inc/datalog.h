#ifndef DATALOG_H
#define DATALOG_H
#include "tagdata.pb.h"

// Stored Data Log -- in external memory

#define DATALOG_SAMPLES 15
_Static_assert(sizeof(((BitTagNgLog *)0)->activity) /
                   sizeof(((BitTagNgLog *)0)->activity[0]) == DATALOG_SAMPLES,
               "BitTagNG protobuf activity array does not match log page");

typedef struct {
  uint16_t activity[DATALOG_SAMPLES];
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
