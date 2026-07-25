#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#define DEBUGVERSION 0x20

// monitor interrupt handler opcodes

enum DBGOP {
  TAG_MONITORINFO = 0,  // returns *sMonitor
  MONITORSTART,         // returns 0 (fail) 1 (success)
  MONITORSTOP,
  PROTOBUF,             // returns packet length
};


// operands for MONITORINFO

enum DBGINFO { MONITORVERSION = 0, MONITORBUF = 1, MONITORBUFSIZE = 2, TAGSHASTR = 3};

#ifdef __cplusplus
extern "C" {
#endif

void monitorServicePending(uint32_t monitor_events);
void monitorPostPendingEvents(void);

#ifdef __cplusplus
}
#endif

#endif
