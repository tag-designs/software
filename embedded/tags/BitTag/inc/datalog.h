#ifndef BITTAG_DATALOG_H
#define BITTAG_DATALOG_H

#include <stdint.h>

#include "persistent.h"
#include "tag.pb.h"

typedef struct
{
  int32_t epoch;
  int16_t temp10;
  uint16_t vdd100;
  uint64_t activity;
} t_DataHeader __attribute__((aligned(8)));

extern t_DataHeader vddHeader[];

enum LOGERR writeDataLog(uint64_t activity);
int restoreLog(void);
int data_logAck(int index, Ack *ack);

#endif /* BITTAG_DATALOG_H */
