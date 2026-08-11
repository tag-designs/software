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

#define BACKUP_STATE_VALID_MAGIC 0x54414742U

#if !defined(TAG_STM32U3_FLASH)
#define TAG_STM32U3_FLASH 0
#endif

#if TAG_STM32U3_FLASH
/**
 * @brief One-shot retained marker for Stop3 wakeups that reset instead of SBF.
 */
#define TAG_SYNTHETIC_STANDBY_WAKE_MAGIC 0x53544259U
#endif

#if !defined(TAG_MONITOR_RESET_RECOVERY)
#define TAG_MONITOR_RESET_RECOVERY 0
#endif

/** @name Persistent linker symbols
 * Linker-provided addresses used to find the internal flash region reserved
 * for state and configuration persistence.
 * @{
 */

extern uint32_t __persistent_start__; ///< Start of the persistent flash region.
extern uint32_t __persistent_end__;   ///< End of the persistent flash region.
extern uint32_t __flash0_end__;       ///< End of the MCU internal flash image.
/** @} */

/** @name Reset and sleep state types
 * Compact values stored across resets so the firmware can explain why it
 * restarted and choose the next state-machine transition.
 * @{
 */
typedef enum
{
  resetSleep,     ///< Return from sleep mode.
  resetStandby,   ///< Return from standby mode.
  resetShutdown,  ///< Return from shutdown mode.
  resetException, ///< Unplanned exception reset.
  resetBrownout,  ///< Brownout other than shutdown.
  resetPower      ///< Power-on reset.
} t_resetCause;

typedef enum
{
  modeSleep,    ///< Runtime should enter ordinary sleep.
  modeStandby,  ///< Runtime should enter standby.
  modeShutdown  ///< Runtime should enter shutdown.
} t_sleepMode;

typedef enum
{
  SYSNOP,   ///< No pending monitor command.
  SYSSTART, ///< Start collection command.
  SYSSTOP,  ///< Stop collection command.
  SYSRESET, ///< Erase/reset command.
  MAXCMD    ///< Sentinel count for command values.
} t_command;
/** @} */

/** @name Backup register mirror
 * Runtime state held in backup/retained storage so reset handling and monitor
 * status can recover state across low-power transitions.
 * @{
 */
typedef struct
{
  uint32_t valid;       ///< Backup registers hold BACKUP_STATE_VALID_MAGIC.
  uint32_t safe;        ///< Marks code regions where reset recovery is safe.
  uint32_t resetCause;  ///< Last reset cause; deprecated.
  uint32_t state;       ///< Current protobuf TagState value.
  uint32_t pages;       ///< Internal log/header pages written.
  int32_t lastactstart; ///< Unix seconds at the last active-state start.
  // runtime
  int32_t temp10;       ///< Running average temperature in 0.1 C units.
  uint32_t vdd100;      ///< Running average supply voltage in 0.01 V units.
  uint64_t activity;    ///< Activity bitmap accumulated by activity tags.
  int32_t lastwakeup;   ///< Unix seconds at the last wakeup.
  int32_t lastwrite;    ///< Unix seconds at the last log write.
  TestResult test_result; ///< Most recent self-test result.
  uint32_t external_blocks; ///< External data blocks/pages written.
#if TAG_STM32U3_FLASH
  uint32_t synthetic_standby_wake; ///< One-shot marker for reset-after-Stop3 wake.
#endif
} BackupState;

/** @brief Retained runtime state mirror in backup storage. */
extern volatile BackupState *const pState;
/** @} */

/** @name Persistent data formats
 * Flash-resident records read directly by boot, monitor, and log recovery code.
 * @{
 */

/**
 * @enum LOGERR
 * @brief Persistent log write result.
 */
enum LOGERR
{
  LOGWRITE_OK,    ///< Write completed successfully.
  LOGWRITE_BAT,   ///< Write stopped because supply/battery was too low.
  LOGWRITE_FULL,  ///< Destination storage is full.
  LOGWRITE_ERROR  ///< Hardware, encoding, or flash programming error.
};

/**
 * @brief Flash-resident marker for one state-machine transition.
 *
 * @details The state machine appends these records in STM32 internal flash so
 *          reset handling can recover the last durable state, event reason, and
 *          log cursors after a power cycle or monitor reset.
 */
typedef struct
{
  int32_t epoch;            ///< Unix seconds when the marker was written.
  TagState state;           ///< State entered by this transition.
  uint32_t internal_pages;  ///< Internal log/header cursor at transition.
  uint32_t external_pages;  ///< External log cursor at transition.
  uint16_t vdd100;          ///< Supply voltage in 0.01 V units.
  int16_t  temp10;          ///< Temperature in 0.1 C units.
  State_Event reason;       ///< Event that caused the transition.
#if TAG_STM32U3_FLASH
  uint64_t flash_padding;   ///< Padding for STM32U3 16-byte flash rows.
} t_StateMarker __attribute__((aligned(16)));
#else
} t_StateMarker __attribute__((aligned(8)));
#endif

/**
 * @brief Legacy activity log record stored in internal flash.
 */
typedef struct
{
  int32_t epoch;     ///< Unix seconds for the activity sample.
  int16_t temp10;    ///< Temperature in 0.1 C units.
  uint16_t vdd100;   ///< Supply voltage in 0.01 V units.
  uint64_t activity; ///< Activity bitmap for the sample window.
} t_DataLog __attribute__((aligned(8)));

/** @brief Last monitor command mirrored into persistent state. */
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
 * @brief Begin an incremental external erase operation.
 */
void eraseExternalStart(void);

/**
 * @brief Erase the next external sector in an incremental erase operation.
 *
 * @return true when more external erase work remains.
 */
bool eraseExternalNextSector(void);

/**
 * @brief Finish an incremental external erase operation.
 */
void eraseExternalFinish(void);

/**
 * @brief Report whether the current incremental external erase failed.
 */
bool eraseExternalFailed(void);

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
 * @brief Report total external sectors expected for the current erase, plus one.
 *
 * @return 0 when unsupported/unknown, otherwise total sectors + 1.
 */
int externalFlashSectorsToErasePlusOne(void);

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
