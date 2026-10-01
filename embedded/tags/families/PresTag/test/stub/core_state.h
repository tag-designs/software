/* stub: the state-handler contract from common/core/inc/core_state.h. */
#ifndef STUB_CORE_STATE_H
#define STUB_CORE_STATE_H
#include <stdbool.h>
#include "tag.pb.h"
enum Sleep { SLEEP, STOP2, STANDBY, SHUTDOWN };
enum StateTrans { T_INIT, T_CONT, T_EXIT, T_ERROR };
enum Sleep Running(enum StateTrans, State_Event reason);
enum Sleep Finished(enum StateTrans, State_Event reason);
enum Sleep Aborted(enum StateTrans, State_Event reason);
enum Sleep Hibernating(enum StateTrans, State_Event reason);
#endif
