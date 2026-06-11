/**
 * @file persistent.h
 * @brief CompassTag persistent runtime state and log metadata layout.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef PERSISTENT_H
#define PERSISTENT_H

#include <stdbool.h>

/** Start of the internal flash region reserved for persistent state. */
extern uint32_t __persistent_start__; // from linker script
/** End of the internal flash region reserved for persistent state. */
extern uint32_t __persistent_end__;   // from linker script
/** End of internal flash, provided by the linker script. */
extern uint32_t __flash0_end__;       // from linker script

/** @brief Reset cause classification recorded across low-power transitions. */
typedef enum
{
  resetSleep,     // return from sleep mode
  resetStandby,   // return from standby mode
  resetShutdown,  // return from shutdown mode
  resetException, // unplanned exception
  resetBrownout,  // brownout other than shutdown
  resetPower      // power on event
} t_resetCause;

/** @brief Low-power mode requested by the state machine. */
typedef enum
{
  modeSleep,
  modeStandby,
  modeShutdown
} t_sleepMode;

/** @brief Monitor command values mirrored in persistent RAM. */
typedef enum
{
  SYSNOP,
  SYSSTART,
  SYSSTOP,
  SYSRESET,
  MAXCMD
} t_command;

/** @brief Backup-register runtime state used to recover after resets. */
typedef struct
{
  uint32_t valid;           // backup registers are valid
  uint32_t safe;            // used to mark regions where reset is safe
  uint32_t resetCause;      // last reset cause -- deprecated, need to remove
  uint32_t state;           // current run state
  uint32_t pages;           // dirty flash pages
  uint32_t external_blocks;  // external data blocks
  int32_t lastactstart;     // time of last active start
  int32_t temp10;           // running average of temperature
  uint32_t vdd100;          // running average of voltage
  uint32_t activity;        // track activity "bits"
  int32_t lastwakeup;       // last wakeup time
  int32_t lastwrite;        // timestamp of last write
  uint32_t cycle_count;      // used for run statemachine to track data
  TestResult test_result;   // test_result

} BackupState;

/** Pointer to the retained backup-state region. */
extern volatile BackupState *const pState;

/********************************************************
 *  Persistent Data Formats [ read directly from flash ]
 *******************************************************/

enum LOGERR
{
  LOGWRITE_OK,
  LOGWRITE_BAT,
  LOGWRITE_FULL,
  LOGWRITE_ERROR
};

/**
 * @brief Internal flash marker for state recovery.
 *
 * The state machine appends one marker on major state transitions so a reset
 * can recover the last known state and log cursors before resuming or aborting.
 */
typedef struct
{
  int32_t epoch;
  TagState state;
  uint32_t internal_pages;
  uint32_t external_pages;
  uint16_t vdd100;
  int16_t temp10;
  State_Event reason;
} t_StateMarker __attribute__((aligned(8)));

/** Last monitor command seen by the firmware. */
extern int32_t monitorCMD;

#define sEPOCH_SIZE (10 + (_TagState_MAX) + 2 * sizeof(((Config *)0)->hibernate) / sizeof(Config_Interval))

/** Internal flash state-transition log. */
extern t_StateMarker sEpoch[sEPOCH_SIZE];
/** @brief Append the current state to the persistent state log. */
void recordState(State_Event reason);
/** @brief Erase internal persistent state and headers. */
void erasePersistent(void);
/** @brief Erase all external log storage. */
void eraseExternal(void);
/** @brief Prepare incremental external log erase. */
void eraseExternalStart(void);
/** @brief Erase the next external sector.
 *
 * @return true when more sectors remain.
 */
bool eraseExternalNextSector(void);
/** @brief Finish incremental external log erase. */
void eraseExternalFinish(void);
/** @brief Erase one external log block, when supported by the target. */
void eraseExternalBlock(void);
/** @brief Report external flash capacity in bytes. */
uint32_t externalFlashSize(void);
/** @brief Report progress while external sectors are being erased. */
int externalFlashSectorsErased(void);
int externalFlashSectorsToErasePlusOne(void);
#endif
