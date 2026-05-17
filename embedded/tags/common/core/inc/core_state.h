#ifndef TAG_CORE_STATE_H
#define TAG_CORE_STATE_H

#include "core_types.h"
#include "tag.pb.h"

enum StateTrans
{
  T_INIT,
  T_CONT,
  T_ERROR
};

extern enum Sleep Configured(enum StateTrans, State_Event reason);
extern enum Sleep Running(enum StateTrans, State_Event reason);
extern enum Sleep Finished(enum StateTrans, State_Event reason);
extern enum Sleep Aborted(enum StateTrans, State_Event reason);
extern enum Sleep Hibernating(enum StateTrans, State_Event reason);
extern enum Sleep Calibrating(enum StateTrans, State_Event reason);

#endif
