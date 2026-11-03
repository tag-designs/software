/**
 * @file core_runtime.h
 * @brief Shared runtime entry points and cross-state activity flags.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_RUNTIME_H
#define TAG_CORE_RUNTIME_H

#include <stdbool.h>

#include "ch.h"
#include "core_types.h"

extern thread_t *tpMain;

/** @name Runtime orchestration
 * Functions that tie together initialization, event processing, the state
 * machine, and final low-power entry.
 * @{
 */
/**
 * @brief Run one state-machine step and return the requested sleep mode.
 *
 * @param[in] events Pending hardware and monitor work events to dispatch.
 * @return Sleep mode requested by the current state.
 */
enum Sleep StateMachine(eventmask_t events);

/**
 * @enum  MonitorCommand
 * @brief Monitor requests that change the tag's state.
 */
typedef enum {
  MONITOR_CMD_START,     ///< Store the configuration and begin a run.
  MONITOR_CMD_STOP,      ///< End a run, an exception or calibration.
  MONITOR_CMD_ERASE,     ///< Erase stored data and return to idle.
  MONITOR_CMD_SELFTEST,  ///< Run the self-tests.
  MONITOR_CMD_CALIBRATE, ///< Enter live sensor calibration.
} MonitorCommand;

/**
 * @brief Carry out a monitor command's state transition at once.
 *
 * @details Called from proto_eval(), so the reply to a state-changing request
 *          reports the state the command reached rather than acknowledging
 *          work a later pass might not do. Start runs Configured(T_INIT);
 *          Stop runs Finished(T_INIT), Aborted(T_INIT) from EXCEPTION, or
 *          leaves calibration; Calibrate runs Calibrating(T_INIT). Erase and
 *          Self-test only enter sRESET or TEST, and the following passes do
 *          the sweep or the test.
 *
 * @param[in] command The command to carry out.
 * @return true when the state allowed the command and its transition ran;
 *         false, with nothing changed, otherwise.
 *
 * @pre StateMachine() has run since boot, so reset recovery is done; main()
 *      serves monitor requests only after a StateMachine() pass.
 * @warning Main thread only.
 */
bool StateMachineCommand(MonitorCommand command);

/**
 * @brief Initialize board devices and persistent runtime state.
 *
 * @param[in] force Nonzero to force reinitialization after reset handling.
 */
void deviceInit(int force);

/**
 * @brief Enter the requested low-power terminal mode after device preparation.
 *
 * @param[in] mode Requested sleep mode.
 */
void godown(enum Sleep mode);
/** @} */

/** Shared activity state used by power and state-machine code. */
extern bool isActive;

/*
 * Idle-thread low-power selector. Valid managed-idle values are SLEEP, STOP0,
 * STOP1, and STOP2; tags without idle power management can ignore it.
 */
extern volatile enum Sleep idlePowerMode;

#endif
