/**
 * @file power_modes.c
 * @brief IMUTagNand ChibiOS idle-thread STOP-mode hooks.
 *
 * @details The idle hook enters the managed sleep mode selected by
 *          idlePowerMode when the monitor is detached. When a monitor is
 *          attached or trying to attach, the hook avoids WFI entirely so the
 *          debug interface and shared-memory monitor requests remain
 *          serviceable.
 */

#include "core_runtime.h"
#include "power.h"

/* Public idle-hook contract documented in power_modes.h. */
void idle_enter(void)
{
}

/* Public idle-hook contract documented in power_modes.h. */
void idle_loop(void)
{
  if (isMonitorEnabled()) {
    return;
  } else if (stIsAlarmActive()) {
    tagPowerEnterIdleMode(SLEEP);
  } else {
   tagPowerEnterIdleMode(idlePowerMode);
  }
}

/* Public idle-hook contract documented in power_modes.h. */
void idle_leave(void)
{
}
