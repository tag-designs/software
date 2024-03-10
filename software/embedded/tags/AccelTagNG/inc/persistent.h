#ifndef PERSISTENT_H
#define PERSISTENT_H

extern uint32_t __persistent_start__; // from linker script
extern uint32_t __flash0_end__;       // from linker script

// Reset causes

typedef enum
{
  resetSleep,     // return from sleep mode
  resetStandby,   // return from standby mode
  resetShutdown,  // return from shutdown mode
  resetException, // unplanned exception
  resetBrownout,  // brownout other than shutdown
  resetPower      // power on event
} t_resetCause;

// Sleep modes

typedef enum
{
  modeSleep,
  modeStandby,
  modeShutdown
} t_sleepMode;

// Statemachine commands

typedef enum
{
  SYSNOP,
  SYSSTART,
  SYSSTOP,
  SYSRESET,
  MAXCMD
} t_command;

typedef struct
{
  uint32_t valid;           // backup registers are valid
  uint32_t safe;            // used to mark regions where reset is safe
  uint32_t resetCause;      // last reset cause -- deprecated, need to remove
  uint32_t state;           // current run state
  uint32_t pages;           // dirty flash pages
  int32_t lastactstart; // time of last active start
  uint32_t external_blocks; // external data blocks
  int32_t temp10;           // running average of temperature
  uint32_t vdd100;          // running average of voltage
  uint32_t activity;        // activity data
  int32_t lastwakeup;   // last wakeup time
  int32_t lastwrite;    // timestamp of last write
  TestResult test_result;   // test_result

} BackupState;

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

// Stored State Log

/*
 * We use the following structure in STM32 flash
 * to track state information that must persist across a power cycle event.
 * Right now we just write the current epoch on entry to the corresponding
 * state.  e.g. runEpoch is written on entry to the RUN state.
 *
 *  location 0 = Triggered
 *  location 1 = Running
 *  location 2 = Aborted/Finished
 *  location 3 = Reset
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

extern int32_t monitorCMD;

#define sEPOCH_SIZE (10 + (_TagState_MAX) + 2 * sizeof(((Config *)0)->hibernate) / sizeof(Config_Interval))

extern t_StateMarker sEpoch[sEPOCH_SIZE];
void recordState(State_Event reason);
void erasePersistent(void);
void eraseExternal(void);
#endif
