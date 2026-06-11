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
 * @brief Collect pending hardware and monitor events into the shared event mask.
 */
eventmask_t CheckEvents(void);

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

#endif
