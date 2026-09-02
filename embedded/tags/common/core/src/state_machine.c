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

#ifndef BACKUP_STATE_VALID_MAGIC
#define BACKUP_STATE_VALID_MAGIC 1U
#endif

#if !defined(CONFIG_HAS_HIBERNATE)
#define CONFIG_HAS_HIBERNATE 1
#endif

#ifndef TAG_MONITOR_RESET_RECOVERY
#define TAG_MONITOR_RESET_RECOVERY 0
#endif

#ifndef TAG_CONFIGURED_IMMEDIATE_START
#define TAG_CONFIGURED_IMMEDIATE_START 0
#endif

#ifndef TAG_EXTERNAL_ERASE_SECTORS_PER_PASS
/**
 * @brief External erase sectors processed before yielding to monitor/status.
 */
#define TAG_EXTERNAL_ERASE_SECTORS_PER_PASS 16U
#endif

#if TAG_EXTERNAL_ERASE_SECTORS_PER_PASS == 0U
#error "TAG_EXTERNAL_ERASE_SECTORS_PER_PASS must be at least 1"
#endif

#ifndef TAG_DEFAULT_IDLE_POWER_MODE
/**
 * @brief Returned idle mode used before any scoped runtime wait overrides it.
 */
#define TAG_DEFAULT_IDLE_POWER_MODE SLEEP
#endif

#ifndef TAG_IDLE_SLEEP_MODE
/**
 * @brief Terminal sleep mode requested while the tag is unconfigured idle.
 */
#define TAG_IDLE_SLEEP_MODE STANDBY
#endif

#ifndef TAG_CONFIGURED_SLEEP_MODE
/**
 * @brief Terminal sleep mode requested while waiting for a configured start.
 */
#define TAG_CONFIGURED_SLEEP_MODE STANDBY
#endif

#ifndef TAG_HIBERNATING_SLEEP_MODE
/**
 * @brief Terminal sleep mode requested during configured hibernation windows.
 */
#define TAG_HIBERNATING_SLEEP_MODE STANDBY
#endif

#ifndef TAG_FINISHED_SLEEP_MODE
/**
 * @brief Terminal sleep mode requested after successful collection completion.
 */
#define TAG_FINISHED_SLEEP_MODE STANDBY
#endif

#ifndef TAG_ABORTED_SLEEP_MODE
/**
 * @brief Terminal sleep mode requested after aborted collection.
 */
#define TAG_ABORTED_SLEEP_MODE STANDBY
#endif

/**
 * @brief Recover persistent data-log state after reset.
 *
 * @return Restored log position or a tag-specific recovery status.
 */
extern int restoreLog(void);

/** Shared active-state hint used by standby wake-source configuration. */
bool isActive = false;

/** Shared idle-thread low-power selector for tags that manage CPU idle mode. */
volatile enum Sleep idlePowerMode = TAG_DEFAULT_IDLE_POWER_MODE;

/**
 * @brief True when boot established a wall clock worth scheduling against.
 *
 * @details Cleared when the backup domain was lost and the external RTC could
 *          not supply a replacement time. Transitions that commit the tag to
 *          collecting data are gated on this, because an untrusted clock makes
 *          every start/stop comparison meaningless while leaving the tag drawing
 *          collection current: measured at 1.71 mA against 6.7 uA in standby, a
 *          255x drain that empties a 12 mAh cell in about seven hours.
 *
 * @note Boot-time determination only. It is not re-evaluated once the host
 *       synchronizes the clock; a host sync moves the tag out of the terminal
 *       state it was parked in.
 *
 * @note Defaults to true because reset recovery, which is the only place the
 *       clock's validity is in question, runs solely for power, brownout,
 *       exception, and unspecified-state boots and overwrites this. An ordinary
 *       standby or shutdown wake retains the backup domain, so the clock is as
 *       good as it was when the tag went to sleep; defaulting to false would
 *       silently stop every scheduled run from ever starting.
 */
bool clockTrusted = true;

#if TAG_MONITOR_RESET_RECOVERY
/**
 * @brief Decide whether the current reset is a monitor attach recovery.
 *
 * @param[in] retained_state_valid true when BackupState carries the valid
 *                                 runtime magic value.
 * @return true when debug/monitor state indicates connect-under-reset rather
 *         than field power loss.
 */
static bool monitorResetRecoveryActive(bool retained_state_valid)
{
  if (!retained_state_valid)
    return false;

  /*
   * MONCONNECTED is intentionally narrow because it is also used by runtime
   * sleep paths such as godown(). During connect-under-reset, however, the
   * host can release VC_CORERESET before the state machine makes its boot
   * recovery decision. C_DEBUGEN is enough evidence here, and only here, that
   * the reset was caused by the monitor rather than field power loss.
   */
  if (MONCONNECTED)
    return true;

  return (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U;
}
#endif

/**
 * @brief Decide whether boot should recover RTC time from the external RTC.
 *
 * @param[in] reset_cause Reset cause classified by main.c.
 * @return true when the STM32 RTC may be uninitialized or stale.
 */
static bool shouldRecoverRtcFromExternal(t_resetCause reset_cause)
{
  if (reset_cause == resetBrownout)
    return true;
#if TAG_MONITOR_RESET_RECOVERY
  if (monitorResetRecoveryActive(pState->valid == BACKUP_STATE_VALID_MAGIC))
    return false;
  if (reset_cause == resetPower)
    return true;
#else
  if (MONCONNECTED && (pState->valid == BACKUP_STATE_VALID_MAGIC))
    return false;
  if ((reset_cause == resetPower) && !MONCONNECTED)
    return true;
#endif
  if (!rtcInitializedAtBoot)
    return true;
  return false;
}

/**
 * @brief Validate a state marker read from STM32 internal flash.
 *
 * @details Applied on every target, not only those building monitor reset
 *          recovery. The marker log shares a flash region with provisioned
 *          state that `<tag>-download` does not erase, so a marker slot can
 *          hold bytes never written as a marker. Range-checking the enums stops
 *          the recovery scan adopting such a value as a state to resume into.
 *
 * @param[in] marker Marker record to validate.
 * @return true when state and reason enum values are in protobuf range.
 *
 * @note A range check is necessary but not sufficient: an arbitrary byte in
 *       1.._TagState_MAX still passes. Detecting genuinely stale records needs a
 *       stamped record format.
 */
static bool validStateMarker(const t_StateMarker *marker)
{
  return (marker->state > STATE_UNSPECIFIED) &&
         (marker->state <= _TagState_MAX) &&
         (marker->reason <= _State_Event_MAX);
}

#if TAG_MONITOR_RESET_RECOVERY
/**
 * @brief Validate a retained TagState enum value.
 *
 * @param[in] state Raw retained state value.
 * @return true when the value is a concrete protobuf TagState.
 */
static bool validTagState(uint32_t state)
{
  return (state > STATE_UNSPECIFIED) && (state <= _TagState_MAX);
}
#endif

static bool reset_erase_started;
#if TAG_MONITOR_RESET_RECOVERY
static bool last_recovery_trace_valid;
static uint32_t last_recovery_reset_cause;
static uint32_t last_recovery_monitor;
static uint32_t last_recovery_retained_valid;
static uint32_t last_recovery_demcr;
static uint32_t last_recovery_dhcsr;
static bool unspecified_recovery_logged;
#endif

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
 * @param[in] input_events Pending hardware and monitor work events to dispatch.
 * @return Sleep mode requested by the selected state handler.
 */
enum Sleep StateMachine(eventmask_t input_events)
{
  events = input_events;

  // power outage ?
  // this needs work !  We probably need to detect whether pState
  // is corrupt, if so we can't count on RTC so we have to abort.

  t_resetCause reset_cause = pState->resetCause;
  const bool recovery_started_from_unspecified =
      pState->state == STATE_UNSPECIFIED;
  bool recovered_concrete_state = false;
  /*
   * Computed unconditionally: the corrupt-pState guard below and the clock-trust
   * decision both depend on it, and both must work on targets that do not build
   * with TAG_MONITOR_RESET_RECOVERY.
   */
  const bool retained_state_valid = pState->valid == BACKUP_STATE_VALID_MAGIC;
#if TAG_MONITOR_RESET_RECOVERY
  bool monitor_reset_recovery = monitorResetRecoveryActive(retained_state_valid);
  uint32_t retained_pages = pState->pages;
  uint32_t retained_external_blocks = pState->external_blocks;
  uint32_t retained_state = pState->state;
  uint32_t recovery_demcr = CoreDebug->DEMCR;
  uint32_t recovery_dhcsr = CoreDebug->DHCSR;
  last_recovery_trace_valid = false;
#endif

  if ((reset_cause == resetPower) ||
      (reset_cause == resetBrownout) ||
      (reset_cause == resetException) ||
      (pState->state == STATE_UNSPECIFIED))
  {
    if ((reset_cause != resetPower) &&
        (reset_cause != resetBrownout) &&
        (reset_cause != resetException))
      reset_cause = resetPower;

#if TAG_MONITOR_RESET_RECOVERY
    last_recovery_trace_valid = true;
    last_recovery_reset_cause = (uint32_t)reset_cause;
    last_recovery_monitor = monitor_reset_recovery ? 1U : 0U;
    last_recovery_retained_valid = retained_state_valid ? 1U : 0U;
    last_recovery_demcr = recovery_demcr;
    last_recovery_dhcsr = recovery_dhcsr;
#endif

    // _unhandled_exception() latches EXCEPTION in backup state before reset.
    // Keep that fact across marker recovery so the last RUNNING marker does
    // not make an exception look like a healthy acquisition restart.
    const bool exception_latched =
        (reset_cause == resetException) &&
        (pState->state == TagState_EXCEPTION);

    // figure out what state we're in
    // if we're running, we let the run procedure decide
    // how to handle the possible error/loss of data

    // what if RTC is off from a brownout ?  We need to see if pState was wiped !

#if TAG_MONITOR_RESET_RECOVERY
    if (monitor_reset_recovery && validTagState(retained_state)) {
      /*
       * Connect-under-reset leaves RTC backup registers intact. In that case
       * the live backup state is more current than the flash transition log,
       * which may not yet contain the RUNNING marker when the monitor resets
       * the core shortly after a start command.
       */
      pState->state = (TagState)retained_state;
      recovered_concrete_state = true;
    } else {
#endif
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
        if (!validStateMarker(&marker))
          break;
        pState->state = marker.state;
        recovered_concrete_state = true;
      }
#if TAG_MONITOR_RESET_RECOVERY
    }
#endif
    /*
     * Establish whether boot ended up with a wall clock worth making decisions
     * against. A failed external-RTC read used to be silent, leaving the STM32
     * RTC uninitialized; GetTimeUnixSec() then returns a plausible-looking but
     * wrong epoch, and every time comparison in the state machine silently
     * evaluates against it. See clockTrusted.
     */
    bool clock_recovered = false;
    if (shouldRecoverRtcFromExternal(reset_cause))
    {
      RTCDateTime tim;
      if (tagRtcGetDateTime(&tim) == MSG_OK)
      {
        rtcSetTime(&RTCD1, &tim);
        clock_recovered = true;
      }
      else
      {
        debug_log_printf(
            "state_machine: external RTC read failed, clock untrusted rc=%u\r\n",
            (unsigned)reset_cause);
      }
    }
    /*
     * A wiped pState means the backup domain lost power, so neither the
     * retained state nor the STM32 RTC survived; only an explicit recovery from
     * the external RTC can restore a usable clock.
     */
    clockTrusted =
        clock_recovered || (retained_state_valid && rtcInitializedAtBoot);
    timestamp = GetTimeUnixSec(&timestamp_millis);

    // recover log location
    // this should find pState->pages/pState->external variables.
#if TAG_MONITOR_RESET_RECOVERY
    int logtime = 0;
    if (monitor_reset_recovery)
    {
      /*
       * A monitor attach resets the core while RTC backup state is still live.
       * Keep the live cursors gathered while detached; restoreLog() can only
       * reconstruct page-aligned progress from internal flash headers.
       */
      pState->pages = retained_pages;
      pState->external_blocks = retained_external_blocks;
    }
    else
    {
      logtime = restoreLog();
    }
#else
    int logtime = restoreLog();
#endif
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

#if TAG_MONITOR_RESET_RECOVERY
    if (recovery_started_from_unspecified && !unspecified_recovery_logged)
    {
      unspecified_recovery_logged = true;
      debug_log_printf(
          "state_machine: unspecified recovery rc=%u valid=%x mon=%u "
          "ret=%u recovered=%u st=%u pages=%u ext=%u de=%x dh=%x\r\n",
          (unsigned)reset_cause, (unsigned)pState->valid,
          monitor_reset_recovery ? 1U : 0U, (unsigned)retained_state,
          recovered_concrete_state ? 1U : 0U, (unsigned)pState->state,
          (unsigned)pState->pages, (unsigned)pState->external_blocks,
          (unsigned)recovery_demcr, (unsigned)recovery_dhcsr);
    }
#endif

    if (exception_latched)
    {
      return Aborted(T_INIT, State_EVENT_EXCEPTION);
    }
    /*
     * pState claims to be valid, yet carries no concrete state, and internal
     * flash offered no marker either. That combination is self-inconsistent, so
     * abort rather than guess.
     *
     * retained_state_valid is deliberately part of this condition. Without it
     * the test would also fire on a freshly programmed tag -- no retained state
     * and no markers -- and strand it in ABORTED before first use. A wiped
     * pState with no markers is instead safe to treat as idle: any tag that had
     * been collecting would have left a RUNNING marker, because Running(T_INIT)
     * records one.
     */
    if (recovery_started_from_unspecified &&
        retained_state_valid &&
        !recovered_concrete_state)
    {
      return Aborted(T_INIT, State_EVENT_UNKNOWN);
    }

    if (pState->state == TagState_CONFIGURED)
    {
      if (reset_cause == resetBrownout)
      {
        return Configured(T_INIT, State_EVENT_BROWNOUT);
      }
#if TAG_MONITOR_RESET_RECOVERY
      if (monitor_reset_recovery)
      {
        return Configured(T_CONT, State_EVENT_POWERFAIL);
      }
#else
      if (MONCONNECTED)
      {
        return Configured(T_CONT, State_EVENT_POWERFAIL);
      }
#endif
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
#if TAG_MONITOR_RESET_RECOVERY
            if (monitor_reset_recovery)
#else
            if (MONCONNECTED)
#endif
              return Hibernating(T_CONT, State_EVENT_POWERFAIL);
            return Aborted(T_INIT, State_EVENT_POWERFAIL);
        }
  
    }
#endif

    if (pState->state == TagState_RUNNING)
    {
      /*
       * A monitor attach connects under reset, so a healthy running tag arrives
       * here looking like a power event. It must resume: Running(T_CONT,
       * POWERFAIL) restarts the sensors, keeps the recovered log cursors, and
       * marks the discontinuity in the next checkpoint. Aborting instead loses
       * the run every time a host connects.
       *
       * The clock-trust gate below therefore applies only when no monitor is
       * present. Its purpose is to stop a field power event leaving the tag
       * collecting against a meaningless stop time; with a host attached the
       * clock can simply be resynchronized, so refusing to resume would trade a
       * real run for a hypothetical one. This matters because clock trust
       * depends on rtcInitializedAtBoot, which on STM32U3 comes from an external
       * RTC query issued before halInit() and is not reliable enough to justify
       * discarding an active acquisition.
       */
#if TAG_MONITOR_RESET_RECOVERY
      const bool monitor_present = monitor_reset_recovery;
#else
      const bool monitor_present = MONCONNECTED;
#endif

      // goto error
      switch (reset_cause)
      {
        case resetSleep:
        case resetStandby:
        case resetShutdown:
          return Running(T_CONT, State_EVENT_OK);
        case resetException:
          if (monitor_present)
            return Running(T_CONT, State_EVENT_POWERFAIL);
          return Aborted(T_INIT, State_EVENT_EXCEPTION);
        case resetBrownout:
          /*
           * Check the monitor first: an attach classified as a brownout should
           * resume and resync rather than restart the run from its beginning.
           */
          if (monitor_present)
            return Running(T_CONT, State_EVENT_POWERFAIL);
          if (!clockTrusted)
          {
            debug_log_printf(
                "state_machine: brownout restart refused, clock untrusted\r\n");
            return Aborted(T_INIT, State_EVENT_POWERFAIL);
          }
          return Running(T_INIT, State_EVENT_BROWNOUT);
        default:
          if (monitor_present)
            return Running(T_CONT, State_EVENT_POWERFAIL);
          return Aborted(T_INIT, State_EVENT_POWERFAIL);
      }
    }
  }

  // check monitor events

  if (events & MON_WORK_SELFTEST)
  {
    if (pState->state == TagState_IDLE)
    {
      return SelfTest(T_INIT, State_EVENT_OK);
    }
  }

  if (events & MON_WORK_START)
  {
    if (pState->state == TagState_IDLE)
    {
      return Configured(T_INIT, State_EVENT_STARTCMD);
    }
  }
  if (events & MON_WORK_STOP)
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
    if (pState->state == TagState_EXCEPTION)
    {
      return Aborted(T_INIT, State_EVENT_STOPCMD);
    }
#if defined(SENSOR_CALIBRATION) && SENSOR_CALIBRATION
    if (pState->state == TagState_CALIBRATE)
    {
      (void)Calibrating(T_EXIT, State_EVENT_STOPCMD);
      return Idle(T_INIT, State_EVENT_OK);
    }
#endif
  }
  if (events & MON_WORK_RESET)
  {
    if ((pState->state == TagState_ABORTED) ||
        (pState->state == TagState_FINISHED) ||
        (pState->state == TagState_sRESET))
    {
      return Reset(T_INIT, State_EVENT_RESETCMD);
    }
  }

  #if defined(SENSOR_CALIBRATION) && SENSOR_CALIBRATION
  if (events & MON_WORK_CALIBRATE)
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
  bool erase_more = false;

  // we need some error recovery here.  If the
  // internal flash isn't correctly marked, then
  // we need to discover the number of dirty pages
  //  if ((pState->state != ABORTED) || (pState->state != FINISHED))
  //  also, what if we've previously entered the reset state ???

  if (t == T_INIT)
  {
    pState->state = TagState_sRESET;
    recordState(reason);
    reset_erase_started = false;
  }

  if (!reset_erase_started)
  {
    restoreLog();
    eraseExternalStart();
    reset_erase_started = true;
  }

  for (uint32_t sector = 0U; sector < TAG_EXTERNAL_ERASE_SECTORS_PER_PASS;
       sector++) {
    erase_more = eraseExternalNextSector();
    if (!erase_more)
      break;
  }

  if (erase_more) {
    return SLEEP;
  }

  eraseExternalFinish();
  const bool external_erase_failed = eraseExternalFailed();
  reset_erase_started = false;

  /*
   * An external erase failure must not strand the tag.
   *
   * This used to return Aborted(EXTERNALFULL). ABORTED cannot reach IDLE, and
   * IDLE is required by Req_start_tag and Req_test_tag -- including the
   * self-test that calls gd5fProvisionLogicalMap() and so repairs an invalid
   * NAND logical map, which is the most common cause of this very failure.
   * A tag therefore looped sRESET -> ABORTED with no route back: observed over
   * seven consecutive reset attempts on hardware, each failing in the same
   * second, with the marker log filling towards the point where recordState()
   * silently stops recording.
   *
   * Clear internal state and return to idle regardless, so the tag stays
   * reachable and repairable. External storage may still hold data; a
   * subsequent run will program over it and report its own errors, which is a
   * far better failure mode than a tag that cannot be commanded at all.
   */
  erasePersistent();

  if (external_erase_failed) {
    debug_log_printf(
        "state_machine: external erase failed during reset; returning to idle "
        "so the NAND map can be re-provisioned by self-test\r\n");
  }

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
 * @return Standby sleep mode while idle.
 */
static enum Sleep Idle(enum StateTrans t, State_Event reason)
{
  /*
   * Idle deliberately records no state marker, unlike every other handler.
   * Reset recovery seeds pState->state with TagState_IDLE before walking
   * sEpoch, so an empty log already resolves to idle; a marker would add
   * nothing and would spend an entry in a fixed-size, non-wrapping log.
   */
  (void)reason;

  if (t == T_INIT)
  {
    /*
     * The invariant is idle => clean state and configuration, which is what
     * erasePersistent() leaves behind. Several paths arrive here without
     * erasing: a reflash preserves the persistent region by design, and
     * SelfTest() and reset recovery both enter idle directly. Claiming idle
     * over stale contents is what allowed a start command to program a
     * configuration on top of an existing one, corrupting it. Refuse instead,
     * so the host has to reset.
     *
     * Checked on entry only. It was previously evaluated on every call to this
     * handler, including T_CONT, which placed internal flash reads --
     * FLASH_ClearEccErrors() and an ECC probe read -- immediately in front of
     * every attempt to enter Stop3, and cost the tag its deep sleep: idle drew
     * 1.71 mA against 6.6 uA, while FINISHED, whose handler performs no flash
     * access, slept correctly. Entry is in any case the only moment at which
     * the condition can change.
     */
#if TAG_STORED_CONFIG_OWN_PAGE
    if (!persistentIdleStateClean())
    {
      debug_log_printf(
          "state_machine: idle refused, persistent state not clean "
          "(config erased=%u); reset required\r\n",
          storedConfigErased() ? 1U : 0U);
      return Aborted(T_INIT, State_EVENT_UNKNOWN);
    }
#endif

    disableAllAlarms();
    disableTicker();
    pState->state = TagState_IDLE;
  }
  return TAG_IDLE_SLEEP_MODE;
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
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
    pState->run_heartbeat = 0;
    pState->terminal_state = STATE_UNSPECIFIED;
    pState->terminal_reason = State_EVENT_UNSPECIFIED;
    pState->header_status = LOGWRITE_OK;
    pState->header_flasherr = 0;
    pState->header_page = 0;
    pState->header_addr = 0;
    pState->header_retries = 0;
    pState->sample_error_count = 0;
    pState->sample_fifo_overruns = 0;
    pState->sample_fifo_watermark_shorts = 0;
    pState->sample_fifo_empty_reads = 0;
    pState->sample_fifo_short_blocks = 0;
#endif
    recordState(reason);

    // write configuration to memory if this
    // is in response to a start command.

    if (reason == State_EVENT_STARTCMD)
      writeStoredConfig(&config_tmp);

#if TAG_CONFIGURED_IMMEDIATE_START
    /*
     * Deliberately not gated on clockTrusted. sconfig.start is computed from
     * timestamp at the moment the start command is accepted
     * (config_tmp.start = (start_delay - 1) * 60 + timestamp), so this is a
     * relative comparison against a value derived from the same clock and is
     * self-consistent whether or not the absolute wall time is trustworthy.
     * Gating it delayed every start to the next minute alarm.
     */
    if (timestamp >= sconfig.start) {
      debug_log_printf("state_machine: immediate start timestamp=%d start=%d\r\n",
                       timestamp, sconfig.start);
      return Running(T_INIT, State_EVENT_STARTTIM);
    }
#endif

    // enable wakeup timer

    enableAlarm(1, ALARM_MINUTE);
    //delayAlarmEpoch(1, sconfig.start);
  }
  else
  {
    //if (sconfig.start < 0) // sanity check !
    //  return Aborted(T_INIT, State_EVENT_STARTTIM);

    debug_log_printf("timestamp %d start %d\n\r",timestamp,sconfig.start);
    /* Relative to the start moment; see the T_INIT comment above. */
    if (timestamp >= sconfig.start) {// look at stored value --
      disableAlarm(1);
      return Running(T_INIT, State_EVENT_STARTTIM);
    }
    enableAlarm(1, ALARM_MINUTE);
  }
  return TAG_CONFIGURED_SLEEP_MODE;
}

#if CONFIG_HAS_HIBERNATE
/**
 * @brief Keep the tag asleep through configured hibernation intervals.
 *
 * @param[in] t Transition phase for the hibernating state.
 * @param[in] reason Event that caused hibernating handling.
 * @return Standby while hibernating, Running when activity resumes, or Finished.
 */
enum Sleep Hibernating(enum StateTrans t, State_Event reason)
{
  if (t == T_INIT)
  {
    tagDevicesApplyPowerState(TAG_DEVICE_POWER_RUNTIME_DEINIT, pState->state);
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
      return TAG_HIBERNATING_SLEEP_MODE;
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
 * @return Terminal sleep mode after recording the finished state.
 */
enum Sleep Finished(enum StateTrans t, State_Event reason)
{
  if (t == T_INIT)
  {
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
    pState->terminal_state = TagState_FINISHED;
    pState->terminal_reason = reason;
#endif
#if TAG_MONITOR_RESET_RECOVERY
    if (last_recovery_trace_valid) {
      debug_log_printf(
        "state_machine: finished r=%d st=%d rc=%u mon=%u val=%u pg=%u ex=%u\r\n",
        reason, pState->state, last_recovery_reset_cause,
        last_recovery_monitor, last_recovery_retained_valid,
        (unsigned)pState->pages, (unsigned)pState->external_blocks);
    } else {
      debug_log_printf(
        "state_machine: finished r=%d st=%d pg=%u ex=%u\r\n",
        reason, pState->state, (unsigned)pState->pages,
        (unsigned)pState->external_blocks);
    }
#endif
    pState->state = TagState_FINISHED;
    recordState(reason);
    deviceInit(true);
  }
  return TAG_FINISHED_SLEEP_MODE;
}

/**
 * @brief Mark collection aborted and reset devices for terminal sleep.
 *
 * @param[in] t Transition phase for the aborted state.
 * @param[in] reason Event that caused aborted handling.
 * @return Terminal sleep mode after recording the aborted state.
 */
enum Sleep Aborted(enum StateTrans t, State_Event reason)
{
  if (t == T_INIT)
  {
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
    pState->terminal_state = TagState_ABORTED;
    pState->terminal_reason = reason;
#endif
#if TAG_MONITOR_RESET_RECOVERY
    if (last_recovery_trace_valid) {
      debug_log_printf(
        "state_machine: aborted r=%d st=%d rc=%u mon=%u val=%u de=%x dh=%x pg=%u ex=%u\r\n",
        reason, pState->state, last_recovery_reset_cause,
        last_recovery_monitor, last_recovery_retained_valid,
        last_recovery_demcr, last_recovery_dhcsr, (unsigned)pState->pages,
        (unsigned)pState->external_blocks);
    } else {
      debug_log_printf(
        "state_machine: aborted r=%d st=%d pg=%u ex=%u\r\n",
        reason, pState->state, (unsigned)pState->pages,
        (unsigned)pState->external_blocks);
    }
#endif
    pState->state = TagState_ABORTED;
    recordState(reason);
    deviceInit(true);
  }
  return TAG_ABORTED_SLEEP_MODE;
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
