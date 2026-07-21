/**
 * @file persistent.h
 * @brief IMUTagBreakout persistent runtime state and log metadata layout.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef PERSISTENT_H
#define PERSISTENT_H

#include <stdbool.h>

#include "custom.h"
#include "config.h"

#if !defined(CONFIG_HAS_HIBERNATE)
#define CONFIG_HAS_HIBERNATE 1
#endif

#define BACKUP_STATE_VALID_MAGIC 0x54414742U

#if !defined(IMUTAG_STM32U3_FLASH)
#if defined(STM32U3XX) || defined(STM32U375xx) || defined(STM32U385xx) || defined(BOARD_IMUTagU375)
#define IMUTAG_STM32U3_FLASH 1
#else
#define IMUTAG_STM32U3_FLASH 0
#endif
#endif

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
  uint32_t valid;           // backup registers hold BACKUP_STATE_VALID_MAGIC
  uint32_t safe;            // used to mark regions where reset is safe
  uint32_t resetCause;      // last reset cause -- deprecated, need to remove
  uint32_t state;           // current run state
  uint32_t pages;           // dirty flash pages
  uint32_t external_blocks;  // external data blocks
  //int32_t lastactstart;     // time of last active start
  int32_t rawtemp;          // latest LPS22HH raw temperature in 0.01 C units
  //uint32_t vdd100;          // running average of voltage
  //uint32_t activity;        // track activity "bits"
  //int32_t lastwakeup;       // last wakeup time
  //int32_t lastwrite;        // timestamp of last write
  uint32_t cycle_count;      // used for run statemachine to track data
  uint32_t run_heartbeat;    // retained RUNNING-state iterations for bring-up
  uint32_t terminal_state;    // retained terminal-state diagnostic
  uint32_t terminal_reason;   // retained terminal-transition reason
  uint32_t header_status;     // retained internal-header write status
  uint32_t header_flasherr;   // retained STM32 flash status for header writes
  uint32_t header_page;       // retained internal-header page attempted
  uint32_t header_addr;       // retained internal-header address attempted
  uint32_t header_retries;    // retained internal-header write retry count
  uint32_t sample_error_count; // retained data-sampling error total
  uint32_t sample_fifo_overruns;        // IMU FIFO overrun count
  uint32_t sample_fifo_watermark_shorts; // watermark asserted but count short
  uint32_t sample_fifo_empty_reads;     // FIFO read returned zero pairs
  uint32_t sample_fifo_short_blocks;    // unrecoverable short superframe blocks
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
#if IMUTAG_STM32U3_FLASH
  uint64_t flash_padding;
} t_StateMarker __attribute__((aligned(16)));
#else
} t_StateMarker __attribute__((aligned(8)));
#endif

/** Last monitor command seen by the firmware. */
extern int32_t monitorCMD;

#if CONFIG_HAS_HIBERNATE
#define sEPOCH_SIZE (10 + (_TagState_MAX) + 2 * sizeof(((Config *)0)->hibernate) / sizeof(Config_Interval))
#else
#define sEPOCH_SIZE (10 + (_TagState_MAX))
#endif

/** Internal flash state-transition log. */
extern t_StateMarker sEpoch[sEPOCH_SIZE];
/** @brief Append the current state to the persistent state log. */
void recordState(State_Event reason);
/** @brief Erase internal persistent state and headers. */
void erasePersistent(void);
/** @brief Erase all external log storage. */
void eraseExternal(void);
/** @brief Begin incremental external log erase. */
void eraseExternalStart(void);
/** @brief Erase one external sector; true if more work remains. */
bool eraseExternalNextSector(void);
/** @brief Finish incremental external log erase. */
void eraseExternalFinish(void);
/** @brief Erase one external log block, when supported by the target. */
void eraseExternalBlock(void);
/** @brief Report external flash capacity in bytes. */
uint32_t externalFlashSize(void);
/** @brief Report progress while external sectors are being erased. */
int externalFlashSectorsErased(void);
/** @brief Report total external sectors expected for the current erase, plus one. */
int externalFlashSectorsToErasePlusOne(void);
#endif
