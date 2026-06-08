/**
 * @file state_machine.c
 * @brief Common control state machine for configured, running, and terminal states.
 * @author tag firmware authors
 * @date 2026-05-23
 *
 *  Code to implement the control state machine
 *
 *      RESET     : entered from ABORTED or FINISHED
 *                  under monitor control.  Marked in
 *                  STM32 Flash.  Power cycle causes return
 *                  to this state.
 *
 *                  Final act is to clear
 *                  the STM32 page holding intermediate
 *                  state information
 *
 *      IDLE      : Default initial state (with clean flash)
 *                  Power cycle enters this state if
 *                  STM32 Flash is clean
 *
 *      CONFIGURED : Countdown to start. 
 *
 *      RUNNING   : Data collection mode.  STM32 Flash has a marker
 *                  Power cycle would return to ABORTED state
 * 
 *      HIBERNATING : shutdown, waiting to return to RUNNING state
 *
 *      FINISHED  : Programmed data collection completed. Exit to
 *                  RESET under monitor control. Power cycle returns
 *                  here.
 *
 *      ABORTED   : Data collection mode exited prematurely.  Data written
 *                  to flash can be recovered.  Power-cycle returns here.
 *                  Transfer under monitor control to RESET
 * 
 *      PANIC     : Unexpected system error 
 *
 */

#include "hal.h"
#include <limits.h>

#include "custom.h"

#include "tag.pb.h"
#include "config.h"
#include "device.h"
#include "core_events.h"
#include "core_runtime.h"
#include "core_state.h"
#include "core_sync.h"
#include "flash_internal.h"
#include "persistent.h"
#include "power.h"
#include "rtc_api.h"
#include "test_support.h"
#include "timekeeping.h"
#include "debug_log.h"

#if !defined(CONFIG_HAS_HIBERNATE)
#define CONFIG_HAS_HIBERNATE 1
#endif

/**
 * @brief Recover persistent data-log state after reset.
 *
 * @return Restored log position or a tag-specific recovery status.
 */
extern int restoreLog(void);

/** Shared active-state hint used by standby wake-source configuration. */
bool isActive = false;

/**
 * @brief Forward declaration for the reset cleanup state.
 *
 * @param[in] transition State transition phase.
 * @param[in] reason Event that caused this state action.
 * @return Requested sleep mode after reset handling.
 */
static enum Sleep Reset(enum StateTrans transition, State_Event reason);

/**
 * @brief Forward declaration for the idle state.
 *
 * @param[in] transition State transition phase.
 * @param[in] reason Event that caused this state action.
 * @return Requested sleep mode after idle handling.
 */
static enum Sleep Idle(enum StateTrans transition, State_Event reason);

/**
 * @brief Forward declaration for the self-test state.
 *
 * @param[in] transition State transition phase.
 * @param[in] reason Event that caused this state action.
 * @return Requested sleep mode after self-test handling.
 */
static enum Sleep SelfTest(enum StateTrans transition, State_Event reason);

/** @name State-machine dispatch
 * Dispatch reads pending events, handles reset recovery, accepts monitor
 * commands, and routes continuation work to the current state handler.
 * @{
 */
eventmask_t events = 0;

/**
 * @brief Run one state-machine step and return the requested sleep mode.
 *
 * @return Sleep mode requested by the selected state handler.
 */
enum Sleep StateMachine(void)
{
  // read the event flags -- we do this here so any later
  // arrivals will cause another wakeup

  // we need to pass these events to the states or 
  // pass the alarm information through a shared variable.

  events = chEvtWaitAnyTimeout(ALL_EVENTS, TIME_IMMEDIATE);

  // power outage ?
  // this needs work !  We probably need to detect whether pState
  // is corrupt, if so we can't count on RTC so we have to abort.

  t_resetCause reset_cause = pState->resetCause;

  if ((reset_cause == resetPower) ||
      (reset_cause == resetBrownout) ||
      (pState->state == STATE_UNSPECIFIED))
  {
    if ((reset_cause != resetPower) && (reset_cause != resetBrownout))
      reset_cause = resetPower;

    // figure out what state we're in
    // if we're running, we let the run procedure decide
    // how to handle the possible error/loss of data

    // what if RTC is off from a brownout ?  We need to see if pState was wiped !

    pState->state = TagState_IDLE;
    pState->pages = 0;

    // find the last state

    for (size_t i = 0; i < sEPOCH_SIZE; i++)
    {
      t_StateMarker marker;
      if (FLASH_Read_Checked(&sEpoch[i], &marker, sizeof(marker)))
        break;
      if (marker.epoch == -1)
        break;
      pState->state = marker.state;
    }
    // try to recover the time from the external RTC

    RTCDateTime tim;
    tagRtcGetDateTime(&tim);
    // set it even if get failed (might still be valid)
    rtcSetTime(&RTCD1, &tim);
    timestamp = GetTimeUnixSec(&timestamp_millis);

    // recover log location
    // this should find pState->pages/pState->external variables.
    int logtime = restoreLog();
    // restart clock if possible
    if (logtime > timestamp)
    {
      SetTimeUnixSec(logtime);
      timestamp = GetTimeUnixSec(&timestamp_millis);;
    }

    /*
     * Reset recovery is a one-shot boot decision.  Keep using reset_cause for
     * this dispatch, but prevent later ordinary state-machine passes from
     * re-entering recovery and interpreting freshly written state markers as a
     * new power-fail event.
     */
    pState->resetCause = resetStandby;

    if (pState->state == TagState_CONFIGURED)
    {
      if (reset_cause == resetBrownout)
      {
        return Configured(T_INIT, State_EVENT_BROWNOUT);
      }
      else
      {
        return Aborted(T_INIT, State_EVENT_POWERFAIL);
      }
    }

    /*
     * Active-state reset recovery
     * ---------------------------
     *
     * pState lives in RTC backup registers, and the state markers in internal
     * flash tell us whether the interrupted tag was CONFIGURED, RUNNING, or
     * HIBERNATING. A true power loss while active is still conservative: abort
     * unless the tag-specific handler has an explicit brownout path.
     *
     * Monitor attach is different. The base connects under reset, so a healthy
     * running tag can arrive here with a resetPower-style cause even though the
     * monitor is now attached and the sensors should keep running. MONCONNECTED
     * is therefore used as the filter for "debug reset, not field power loss".
     *
     * We pass T_CONT plus State_EVENT_POWERFAIL to RUNNING/HIBERNATING in that
     * case. T_CONT preserves recovered log cursors; the POWERFAIL reason tells
     * tag-specific code to repair volatile ownership such as mutexes, muxes,
     * timers, or FIFO phase, and to mark any data discontinuity. Ordinary
     * standby/sleep wakeups continue to use State_EVENT_OK.
     */

#if CONFIG_HAS_HIBERNATE
    if (pState->state == TagState_HIBERNATING)
    {
      // goto error
  
        switch (reset_cause)
        {
          // need to distinguish hibernating from running
          case resetSleep:
          case resetStandby:
          case resetShutdown:
          case resetException:
            return Hibernating(T_CONT, State_EVENT_OK);
          case resetBrownout:
            return Hibernating(T_INIT, State_EVENT_BROWNOUT);
          default:
            if (MONCONNECTED)
              return Hibernating(T_CONT, State_EVENT_POWERFAIL);
            return Aborted(T_INIT, State_EVENT_POWERFAIL);
        }
  
    }
#endif

    if (pState->state == TagState_RUNNING)
    {
      // goto error
      switch (reset_cause)
      {
        case resetSleep:
        case resetStandby:
        case resetShutdown:
        case resetException:
          return Running(T_CONT, State_EVENT_OK);
        case resetBrownout:
          return Running(T_INIT, State_EVENT_BROWNOUT);
        default:
          if (MONCONNECTED)
            return Running(T_CONT, State_EVENT_POWERFAIL);
          return Aborted(T_INIT, State_EVENT_POWERFAIL);
      }
    }
  }

  // check monitor events

  if (events & EVT_SELFTEST)
  {
    if (pState->state == TagState_IDLE)
    {
      return SelfTest(T_INIT, State_EVENT_OK);
    }
  }

  if (events & EVT_START)
  {
    if (pState->state == TagState_IDLE)
    {
      return Configured(T_INIT, State_EVENT_STARTCMD);
    }
  }
  if (events & EVT_STOP)
  {
    bool stop_requested =
        (pState->state == TagState_CONFIGURED) ||
        (pState->state == TagState_RUNNING);
#if CONFIG_HAS_HIBERNATE
    stop_requested = stop_requested ||
        (pState->state == TagState_HIBERNATING);
#endif

    if (stop_requested)
    {
      return Finished(T_INIT, State_EVENT_STOPCMD);
    }
#if defined(SENSOR_CALIBRATION) && SENSOR_CALIBRATION
    if (pState->state == TagState_CALIBRATE)
    {
      (void)Calibrating(T_EXIT, State_EVENT_STOPCMD);
      return Idle(T_INIT, State_EVENT_OK);
    }
#endif
  }
  if (events & EVT_RESET)
  {
    if ((pState->state == TagState_ABORTED) ||
        (pState->state == TagState_FINISHED) ||
        (pState->state == TagState_sRESET))
    {
      return Reset(T_INIT, State_EVENT_RESETCMD);
    }
  }

  #if defined(SENSOR_CALIBRATION) && SENSOR_CALIBRATION
  if (events & EVT_CALIBRATE)
  {
    if (pState->state == TagState_IDLE)
    {
      return Calibrating(T_INIT, State_EVENT_OK);
    }
  }
  #endif

  // eval state

  switch (pState->state)
  {
  case TagState_TEST:
    return SelfTest(T_CONT, State_EVENT_OK);
  case TagState_IDLE:
    return Idle(T_CONT, State_EVENT_OK);
  case TagState_CONFIGURED:
    return Configured(T_CONT, State_EVENT_OK);
  case TagState_RUNNING:
    return Running(T_CONT, State_EVENT_OK);
#if CONFIG_HAS_HIBERNATE
  case TagState_HIBERNATING:
    return Hibernating(T_CONT, State_EVENT_OK);
#endif
  case TagState_FINISHED:
    return Finished(T_CONT, State_EVENT_OK);
  case TagState_ABORTED:
    return Aborted(T_CONT, State_EVENT_OK);
  case TagState_sRESET:
    return Reset(T_CONT, State_EVENT_OK);
  case TagState_EXCEPTION:
    return Aborted(T_INIT, State_EVENT_EXCEPTION);
#if defined(SENSOR_CALIBRATION) && SENSOR_CALIBRATION
  case TagState_CALIBRATE:
    return Calibrating(T_CONT, State_EVENT_OK);
#endif

  default:
    // this is an error case and should never reach here
    return Aborted(T_INIT, State_EVENT_UNKNOWN);
  }
}
/** @} */

/** @name Core state handlers
 * State handlers own state-entry side effects such as persistent markers,
 * alarms, erase operations, and device reinitialization.
 * @{
 */
/**
 * @brief Erase persistent state and return the tag to idle.
 *
 * @param[in] t Transition phase for the reset state.
 * @param[in] reason Event that caused reset entry.
 * @return Sleep mode requested after reset cleanup.
 */
static enum Sleep Reset(enum StateTrans t, State_Event reason)
{
  // we need some error recovery here.  If the
  // internal flash isn't correctly marked, then
  // we need to discover the number of dirty pages
  //  if ((pState->state != ABORTED) || (pState->state != FINISHED))
  //  also, what if we've previously entered the reset state ???

  if (t == T_INIT)
  {
    pState->state = TagState_sRESET;
    recordState(reason);
    restoreLog();
  }

  // clean up the persistent state -- External First !
  eraseExternal();
  erasePersistent();

 // pState->logcnt = 0;

  // reset devices (accelerometer)

  deviceInit(true);
  return Idle(T_INIT, State_EVENT_OK);
}

/**
 * @brief Hold the tag in idle until a monitor command starts work.
 *
 * @param[in] t Transition phase for the idle state.
 * @param[in] reason Event that caused idle handling.
 * @return Shutdown sleep mode while idle.
 */
static enum Sleep Idle(enum StateTrans t, State_Event reason)
{
  (void)reason;
  (void)t;
  if (t == T_INIT)
  {
    pState->state = TagState_IDLE;
  }
  return SHUTDOWN;
}


/**
 * @brief Store configuration and wait for the configured start condition.
 *
 * @param[in] t Transition phase for the configured state.
 * @param[in] reason Event that caused configured handling.
 * @return Shutdown while waiting, or Running entry when the start condition fires.
 */
enum Sleep Configured(enum StateTrans t, State_Event reason)
{
  if (t == T_INIT)
  {
    // record the new state

    pState->state = TagState_CONFIGURED;
    recordState(reason);

    // write configuration to memory if this
    // is in response to a start command.

    if (reason == State_EVENT_STARTCMD)
      writeStoredConfig(&config_tmp);

    // enable wakeup timer

    enableAlarm(1, ALARM_MINUTE);
  }
  else
  {
    //if (sconfig.start < 0) // sanity check !
    //  return Aborted(T_INIT, State_EVENT_STARTTIM);

    debug_log_printf("timestamp %d start %d\n\r",timestamp,sconfig.start);
    if (timestamp >= sconfig.start) // look at stored value --
      return Running(T_INIT, State_EVENT_STARTTIM);
  }
  return SHUTDOWN;
}

#if CONFIG_HAS_HIBERNATE
/**
 * @brief Keep the tag asleep through configured hibernation intervals.
 *
 * @param[in] t Transition phase for the hibernating state.
 * @param[in] reason Event that caused hibernating handling.
 * @return Shutdown while hibernating, Running when activity resumes, or Finished.
 */
enum Sleep Hibernating(enum StateTrans t, State_Event reason)
{
  if (t == T_INIT)
  {
    tagDevicesDeinit();
    pState->state = TagState_HIBERNATING;
    recordState(reason);
    // set 1 hour wakeup interval
    disableAllAlarms();
    disableTicker();
    enableAlarm(1, ALARM_HOUR);
  }

  // check if hibernation should continue;

  for (size_t i = 0; i < sizeof(sconfig.hibernate) / sizeof(Config_Interval); i++)
  {
    if ((timestamp >= sconfig.hibernate[i].start_epoch) &&
        (timestamp < sconfig.hibernate[i].end_epoch))
      return SHUTDOWN;
  }

  if (timestamp < sconfig.stop)
  {
    return Running(T_INIT, State_EVENT_ENDHIB);
  }
  else
  {
    return Finished(T_INIT, State_EVENT_ENDTIM);
  }
}
#endif

// Running() is
// in state_run.c

/**
 * @brief Mark collection finished and reset devices for terminal sleep.
 *
 * @param[in] t Transition phase for the finished state.
 * @param[in] reason Event that caused finished handling.
 * @return Shutdown sleep mode.
 */
enum Sleep Finished(enum StateTrans t, State_Event reason)
{
  if (t == T_INIT)
  {
    pState->state = TagState_FINISHED;
    recordState(reason);
    deviceInit(true);
  }
  return SHUTDOWN;
}

/**
 * @brief Mark collection aborted and reset devices for terminal sleep.
 *
 * @param[in] t Transition phase for the aborted state.
 * @param[in] reason Event that caused aborted handling.
 * @return Shutdown sleep mode.
 */
enum Sleep Aborted(enum StateTrans t, State_Event reason)
{
  if (t == T_INIT)
  {
    pState->state = TagState_ABORTED;
    recordState(reason);
    deviceInit(true);
  }
  return SHUTDOWN;
}

/**
 * @brief Run tag self-tests and return to idle with the result retained.
 *
 * @param[in] t Transition phase for the self-test state.
 * @param[in] reason Event that caused self-test handling.
 * @return Sleep mode requested by idle after tests finish.
 */
static enum Sleep SelfTest(enum StateTrans t, State_Event reason)
{
  (void)reason;
  if (t == T_INIT)
  {
    deviceInit(true);
    pState->state = TagState_TEST;
    test();
  }
  return Idle(T_INIT, State_EVENT_OK);
}
/** @} */
