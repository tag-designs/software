/**
 * @file persistent.h
 * @brief Persistent backup state, flash log formats, and storage hooks.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef PERSISTENT_H
#define PERSISTENT_H

#include <stdbool.h>
#include <stdint.h>

#include "custom.h"
#include "config.h"
#include "tag.pb.h"

#if !defined(CONFIG_HAS_HIBERNATE)
#define CONFIG_HAS_HIBERNATE 1
#endif

/** @name Persistent linker symbols
 * Linker-provided addresses used to find the internal flash region reserved
 * for state and configuration persistence.
 * @{
 */

extern uint32_t __persistent_start__; // from linker script
extern uint32_t __persistent_end__;   // from linker script
extern uint32_t __flash0_end__;       // from linker script
/** @} */

/** @name Reset and sleep state types
 * Compact values stored across resets so the firmware can explain why it
 * restarted and choose the next state-machine transition.
 * @{
 */
typedef enum
{
  resetSleep,     // return from sleep mode
  resetStandby,   // return from standby mode
  resetShutdown,  // return from shutdown mode
  resetException, // unplanned exception
  resetBrownout,  // brownout other than shutdown
  resetPower      // power on event
} t_resetCause;

typedef enum
{
  modeSleep,
  modeStandby,
  modeShutdown
} t_sleepMode;

typedef enum
{
  SYSNOP,
  SYSSTART,
  SYSSTOP,
  SYSRESET,
  MAXCMD
} t_command;
/** @} */

/** @name Backup register mirror
 * Runtime state held in backup/retained storage so reset handling and monitor
 * status can recover state across low-power transitions.
 * @{
 */
typedef struct
{
  uint32_t valid;       // backup registers are valid
  uint32_t safe;        // used to mark regions where reset is safe
  uint32_t resetCause;  // last reset cause -- deprecated, need to remove
  uint32_t state;       // current run state
  uint32_t pages;       // dirty flash pages
  int32_t lastactstart; // time of last active start
  // runtime
  int32_t temp10;       // running average of temperature
  uint32_t vdd100;      // running average of voltage
  uint64_t activity;    // track activity "bits"
  int32_t lastwakeup;   // last wakeup time
  int32_t lastwrite;    // timestamp of last write
  TestResult test_result;   // test_result
  uint32_t external_blocks; // external data blocks
} BackupState;

extern volatile BackupState *const pState;
/** @} */

/** @name Persistent data formats
 * Flash-resident records read directly by boot, monitor, and log recovery code.
 * @{
 */

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
  int16_t  temp10;
  State_Event reason;
} t_StateMarker __attribute__((aligned(8))); 

typedef struct
{
  int32_t epoch;
  int16_t temp10;
  uint16_t vdd100;
  uint64_t activity;
} t_DataLog __attribute__((aligned(8)));

extern int32_t monitorCMD;

#if CONFIG_HAS_HIBERNATE
#define sEPOCH_SIZE (10+(_TagState_MAX) + 2*sizeof(((Config *) 0)->hibernate)/sizeof(Config_Interval))
#else
#define sEPOCH_SIZE (10+(_TagState_MAX))
#endif

extern t_StateMarker sEpoch[sEPOCH_SIZE];
extern t_storedconfig sconfig;
extern t_DataLog vddState[];
extern t_storedconfig config_tmp;
/** @} */

/** @name Persistent storage operations
 * Functions that erase, append, and recover the internal/external persistent
 * records needed by reset handling, monitor commands, and data logging.
 * @{
 */
/**
 * @brief Erase the internal flash region reserved for persistent records.
 */
void erasePersistent(void);

/**
 * @brief Erase all external data-log storage when a tag provides it.
 */
void eraseExternal(void);

/**
 * @brief Erase one external storage block when a tag provides block erasure.
 */
void eraseExternalBlock(void);

/**
 * @brief Report the size of tag-provided external storage.
 *
 * @return External storage capacity in bytes, or 0 when absent.
 */
uint32_t externalFlashSize(void);

/**
 * @brief Report how many external sectors have been erased in the current operation.
 *
 * @return Count of erased external sectors, or 0 when absent.
 */
int externalFlashSectorsErased(void);

/**
 * @brief Persist a validated stored-configuration image.
 *
 * @param[in] s Stored configuration image to write.
 */
void writeStoredConfig(t_storedconfig *s);

/**
 * @brief Append one activity entry to the data log.
 *
 * @param[in] activity Activity bitmap to persist with current status data.
 * @return Log write status explaining success or the reason writing stopped.
 */
enum LOGERR writeDataLog(uint64_t activity);

/**
 * @brief Append a state transition marker to internal flash.
 *
 * @param[in] reason Event that caused the state transition.
 */
void recordState(State_Event reason);

/**
 * @brief Persist the active protobuf configuration.
 *
 * @param[in] config Configuration to encode and write.
 * @return true when the configuration was written successfully.
 */
bool writeConfig(Config *config);

/**
 * @brief Load the active protobuf configuration from persistent storage.
 *
 * @param[out] config Configuration object to populate.
 */
void readConfig(Config *config);

/**
 * @brief Recover persistent data-log state after reset.
 *
 * @return Restored log position or a tag-specific recovery status.
 */
int restoreLog(void);
/** @} */
#endif
