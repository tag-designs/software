#ifndef STUB_PERSISTENT_H
#define STUB_PERSISTENT_H
#include <stdint.h>
#include <stdbool.h>
#include "tag.pb.h"
enum LOGERR { LOGWRITE_OK, LOGWRITE_BAT, LOGWRITE_FULL, LOGWRITE_ERROR };
typedef struct {
  uint32_t valid, safe, resetCause, state, pages, external_blocks;
  int32_t lastactstart, temp10;
  uint32_t vdd100, activity;
  int32_t lastwakeup, lastwrite;
  TestResult test_result;
} BackupState;
extern volatile BackupState *const pState;
void recordState(State_Event reason);
#endif
