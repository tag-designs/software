/**
 * @file state_machine.c
 * @brief CompassTag control state machine and reset recovery.
 * @author tag firmware authors
 * @date 2026-05-23
 *
 * RESET is entered from ABORTED or FINISHED under monitor control and clears
 * intermediate persistent state. IDLE is the clean default. CONFIGURED waits
 * for the programmed start. RUNNING is data collection. HIBERNATING sleeps
 * through scheduled gaps. FINISHED and ABORTED preserve enough log state for
 * the monitor to recover data.
 */

#include "hal.h"
#include <limits.h>

#include "custom.h"

#if defined(TAG_SENSOR_ACCEL_ADXL362)
#include "ADXL362.h"
#endif

#if defined(USE_ADXL367)
#include "ADXL367.h"
#endif

#include "tag.pb.h"
#include "config.h"
#include "core_events.h"
#include "core_runtime.h"
#include "core_state.h"
#include "core_sync.h"
#include "devices.h"
#include "persistent.h"
#include "power.h"
#include "rtc_api.h"
#include "test_support.h"
#include "timekeeping.h"

extern int restoreLog(void);

// a global variable !!
// initialized to false here, capture adxl pin in running
// this is used to determine wakeup edge in pwr.c

bool isActive = false;

static enum Sleep Reset(enum StateTrans, State_Event);
static enum Sleep Idle(enum StateTrans, State_Event);
static enum Sleep SelfTest(enum StateTrans, State_Event);

/*******************************************************************
 *   State machine provides tag execution logic
 *******************************************************************/

eventmask_t events = 0;
/**
 * @brief Dispatch monitor, RTC, reset, and state-continuation events.
 *
 * @return Requested low-power mode after the selected state handler runs.
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

  if ((pState->resetCause == resetPower) ||
      (pState->resetCause == resetBrownout) ||
      (pState->state == STATE_UNSPECIFIED))
  {
    // figure out what state we're in
    // if we're running, we let the run procedure decide
    // how to handle the possible error/loss of data

    // what if RTC is off from a brownout ?  We need to see if pState was wiped !

    pState->state = TagState_IDLE;
    pState->pages = 0;

    // find the last state

    for (size_t i = 0; i < sEPOCH_SIZE; i++)
    {
      if (sEpoch[i].epoch == -1)
        break;
      //pState->pages = sEpoch[i].internal_pages;
      pState->state = sEpoch[i].state;
      timestamp = sEpoch[i].epoch;
    }
    // try to recover the time from the external RTC

    //RTCDateTime tim;
    //getRTCDateTime(&tim);
    // set it even if get failed (might still be valid)
    //rtcSetTime(&RTCD1, &tim);
    //timestamp = GetTimeUnixSec(&timestamp_millis);

    

    SetTimeUnixSec(timestamp);

    // recover log location
    // this should find pState->pages/pState->external variables.
    int logtime = restoreLog();
    // restart clock if possible
    if (logtime > timestamp)
    {
      SetTimeUnixSec(logtime);
      timestamp = GetTimeUnixSec(&timestamp_millis);;
    }

    if (pState->state == TagState_CONFIGURED)
    {
      if (pState->resetCause == resetBrownout)
      {
        return Configured(T_INIT, State_EVENT_BROWNOUT);
      }
      else
      { // try to restart anyway
        return Aborted(T_INIT, State_EVENT_POWERFAIL);
      }
    }

    // hand reset conditions for hibernating and running separately

     if (pState->state == TagState_HIBERNATING)
    {
      // goto error
  
        switch (pState->resetCause)
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
            return Aborted(T_INIT, State_EVENT_POWERFAIL);
        }
  
    }

    if (pState->state == TagState_RUNNING)
    {
      // goto error
      switch (pState->resetCause)
      {
        case resetSleep:
        case resetStandby:
        case resetShutdown:
        case resetException:
          return Running(T_CONT, State_EVENT_OK);
        case resetBrownout:
          return Running(T_INIT, State_EVENT_BROWNOUT);
        case resetPower:
          return Running(T_INIT, State_EVENT_POWERFAIL);
        default:
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
    if ((pState->state == TagState_CONFIGURED) ||
        (pState->state == TagState_RUNNING) ||
        (pState->state == TagState_HIBERNATING))
    {
      return Finished(T_INIT, State_EVENT_STOPCMD);
    }
    if (pState->state == TagState_CALIBRATE)
    {
      return Idle(T_INIT, State_EVENT_OK);
    }
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
  case TagState_HIBERNATING:
    return Hibernating(T_CONT, State_EVENT_OK);
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

/**
 * @brief Erase persistent data and return the tag to IDLE.
 *
 * @param[in] t State transition phase.
 * @param[in] reason Reason for entering reset.
 * @return Requested low-power mode after reset handling.
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
 * @brief Hold the tag in idle until a monitor command arrives.
 *
 * @param[in] t State transition phase.
 * @param[in] reason Reason for entering idle.
 * @return Requested low-power mode while idle.
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
 * @brief Persist start configuration and wait for the acquisition start time.
 *
 * @param[in] t State transition phase.
 * @param[in] reason Reason for entering configured state.
 * @return Requested low-power mode or transition to RUNNING.
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

    enableAlarm(1, ALARM_HOUR);
  }
  else
  {
    //if (sconfig.start < 0) // sanity check !
   //   return Aborted(T_INIT, State_EVENT_STARTTIM);
    //if (timestamp >= sconfig.start) // look at stored value --
      return Running(T_INIT, State_EVENT_STARTTIM);
  }
  return SHUTDOWN;
}

/**
 * @brief Sleep through configured hibernation windows.
 *
 * @param[in] t State transition phase.
 * @param[in] reason Reason for entering hibernation.
 * @return Requested low-power mode or transition back to acquisition.
 */
enum Sleep Hibernating(enum StateTrans t, State_Event reason)
{
  if (t == T_INIT)
  {
    // disable I/O devices
#if defined(TAG_SENSOR_ACCEL_ADXL362)
    ADXL362_DeinitDevice(tagAdxl362Device());
#endif
#if defined(USE_ADXL367)
    accelSpiOn();
    ADXL367_SoftwareReset();
    accelSpiOff();
#endif
    lis2du12Deinit(TAG_ACCEL_DEVICE);
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

// Running() is
// in state_run.c

/**
 * @brief Mark the run complete and leave data available for the monitor.
 *
 * @param[in] t State transition phase.
 * @param[in] reason Reason for finishing.
 * @return Requested low-power mode.
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
 * @brief Mark the run aborted while preserving recoverable log state.
 *
 * @param[in] t State transition phase.
 * @param[in] reason Reason for aborting.
 * @return Requested low-power mode.
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
 * @brief Run the configured production self-tests from IDLE.
 *
 * @param[in] t State transition phase.
 * @param[in] reason Reason for self-test entry.
 * @return Transition back to IDLE after tests complete.
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
