#ifndef TAG_CORE_RUNTIME_H
#define TAG_CORE_RUNTIME_H

#include <stdbool.h>

#include "ch.h"
#include "core_types.h"

extern thread_t *tpMain;

enum Sleep StateMachine(void);
void CheckEvents(void);
void deviceInit(int force);
void godown(enum Sleep mode);

// Shared activity state used by power and state-machine code.
extern bool isActive;

#endif
