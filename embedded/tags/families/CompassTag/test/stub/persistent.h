/* stub: families/CompassTag/inc/persistent.h, the parts datalog.c and
   state_run.c use. BackupState carries the family's fields. */
#ifndef STUB_PERSISTENT_H
#define STUB_PERSISTENT_H
#include <stdint.h>
#include <stdbool.h>
#include "tag.pb.h"
/* The firmware takes the address of a linker symbol; the harness points it
   at the end of its simulated header array instead. */
extern uint32_t *sim_persistent_end;
#define __persistent_end__ (*sim_persistent_end)
typedef struct {
  uint32_t valid, safe, resetCause, state, pages, external_blocks;
  int32_t lastactstart, temp10;
  uint32_t vdd100, activity;
  int32_t lastwakeup, lastwrite;
  uint32_t cycle_count;
  TestResult test_result;
} BackupState;
extern volatile BackupState *const pState;
enum LOGERR { LOGWRITE_OK, LOGWRITE_BAT, LOGWRITE_FULL, LOGWRITE_ERROR };
typedef struct {
  int32_t epoch;
  TagState state;
  uint32_t internal_pages;
  uint32_t external_pages;
  uint16_t vdd100;
  int16_t temp10;
  State_Event reason;
} t_StateMarker;
#define sEPOCH_SIZE 16
extern t_StateMarker sEpoch[sEPOCH_SIZE];
void recordState(State_Event reason);
void eraseExternal(void);
void eraseExternalStart(void);
bool eraseExternalNextSector(void);
void eraseExternalFinish(void);
uint32_t externalFlashSize(void);
#endif
