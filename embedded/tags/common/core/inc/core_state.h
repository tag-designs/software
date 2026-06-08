/**
 * @file core_state.h
 * @brief State-machine state handler declarations.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_STATE_H
#define TAG_CORE_STATE_H

#include "custom.h"
#include "core_types.h"
#include "tag.pb.h"

#if !defined(CONFIG_HAS_HIBERNATE)
#define CONFIG_HAS_HIBERNATE 1
#endif

/** @name State transitions
 * Transition phase passed into state handlers so each state can separate entry,
 * continuation, normal exit, and error cleanup behavior.
 * @{
 */
enum StateTrans
{
  T_INIT,
  T_CONT,
  T_EXIT,
  T_ERROR
};
/** @} */

/** @name State handlers
 * Handlers return the sleep mode the runtime should use after processing the
 * current event and transition phase.
 * @{
 */
/**
 * @brief Handle the configured-but-not-running state.
 *
 * @param[in] transition State transition phase.
 * @param[in] reason Event that caused this state action.
 * @return Requested sleep mode after handling the state.
 */
extern enum Sleep Configured(enum StateTrans, State_Event reason);

/**
 * @brief Handle the active data-collection state.
 *
 * @param[in] transition State transition phase.
 * @param[in] reason Event that caused this state action.
 * @return Requested sleep mode after handling the state.
 */
extern enum Sleep Running(enum StateTrans, State_Event reason);

/**
 * @brief Handle the finished terminal state.
 *
 * @param[in] transition State transition phase.
 * @param[in] reason Event that caused this state action.
 * @return Requested sleep mode after handling the state.
 */
extern enum Sleep Finished(enum StateTrans, State_Event reason);

/**
 * @brief Handle the aborted terminal state.
 *
 * @param[in] transition State transition phase.
 * @param[in] reason Event that caused this state action.
 * @return Requested sleep mode after handling the state.
 */
extern enum Sleep Aborted(enum StateTrans, State_Event reason);

/**
 * @brief Handle the hibernating state between scheduled activity windows.
 *
 * @param[in] transition State transition phase.
 * @param[in] reason Event that caused this state action.
 * @return Requested sleep mode after handling the state.
 */
#if CONFIG_HAS_HIBERNATE
extern enum Sleep Hibernating(enum StateTrans, State_Event reason);
#endif

/**
 * @brief Handle the sensor calibration state.
 *
 * @param[in] transition State transition phase.
 * @param[in] reason Event that caused this state action.
 * @return Requested sleep mode after handling the state.
 */
#if defined(SENSOR_CALIBRATION) && SENSOR_CALIBRATION
extern enum Sleep Calibrating(enum StateTrans, State_Event reason);
#endif
/** @} */

#endif
