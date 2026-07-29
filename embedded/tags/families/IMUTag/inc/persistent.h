/**
 * @file persistent.h
 * @brief IMUTag family persistent runtime state and log metadata layout.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef PERSISTENT_H
#define PERSISTENT_H

#include <stdbool.h>

#include "custom.h"
#include "config.h"

#if !defined(CONFIG_HAS_HIBERNATE)
/** @brief Enable hibernation-state support unless a target opts out. */
#define CONFIG_HAS_HIBERNATE 1
#endif

/** @brief Magic value marking retained BackupState contents as initialized. */
#define BACKUP_STATE_VALID_MAGIC 0x54414742U

#if !defined(IMUTAG_STM32U3_FLASH)
#if defined(STM32U3XX) || defined(STM32U375xx) || defined(STM32U385xx) || defined(BOARD_IMUTagU375)
/** @brief Use STM32U3 internal-flash row layout for persistent IMUTag data. */
#define IMUTAG_STM32U3_FLASH 1
#else
/** @brief Use legacy internal-flash row layout for persistent IMUTag data. */
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
  resetSleep,     ///< Return from sleep mode.
  resetStandby,   ///< Return from standby mode.
  resetShutdown,  ///< Return from shutdown mode.
  resetException, ///< Unplanned exception.
  resetBrownout,  ///< Brownout other than shutdown.
  resetPower      ///< Power-on event.
} t_resetCause;

/** @brief Low-power mode requested by the state machine. */
typedef enum
{
  modeSleep,    ///< MCU sleep mode.
  modeStandby,  ///< MCU standby mode.
  modeShutdown  ///< MCU shutdown mode.
} t_sleepMode;

/** @brief Monitor command values mirrored in persistent RAM. */
typedef enum
{
  SYSNOP,   ///< No monitor command pending.
  SYSSTART, ///< Start command accepted.
  SYSSTOP,  ///< Stop command accepted.
  SYSRESET, ///< Reset command accepted.
  MAXCMD    ///< Sentinel count for command validation.
} t_command;

/** @brief Backup-register runtime state used to recover after resets. */
typedef struct
{
  uint32_t valid;           ///< Holds BACKUP_STATE_VALID_MAGIC when initialized.
  uint32_t safe;            ///< Nonzero when reset/recovery may trust state.
  uint32_t resetCause;      ///< Last reset cause; deprecated retained field.
  uint32_t state;           ///< Current tag state-machine state.
  uint32_t pages;           ///< Internal checkpoint/header rows written.
  uint32_t external_blocks; ///< External log pages recovered or committed.
  //int32_t lastactstart;     // time of last active start
  int32_t rawtemp;          ///< Latest LPS22HH raw temperature in 0.01 C units.
  //uint32_t vdd100;          // running average of voltage
  //uint32_t activity;        // track activity "bits"
  //int32_t lastwakeup;       // last wakeup time
  //int32_t lastwrite;        // timestamp of last write
  uint32_t cycle_count;      ///< RUNNING-state cycle counter.
  uint32_t run_heartbeat;    ///< Retained RUNNING-state iterations for bring-up.
  uint32_t terminal_state;   ///< Retained terminal-state diagnostic.
  uint32_t terminal_reason;  ///< Retained terminal-transition reason.
  uint32_t header_status;    ///< Retained internal-header write status.
  uint32_t header_flasherr;  ///< Retained STM32 flash status for header writes.
  uint32_t header_page;      ///< Retained internal-header page attempted.
  uint32_t header_addr;      ///< Retained internal-header address attempted.
  uint32_t header_retries;   ///< Retained internal-header write retry count.
  uint32_t checkpoint_flags_pending; ///< Flags to include in the next checkpoint row.
  uint32_t sample_error_count; ///< Retained data-sampling error total.
  uint32_t sample_fifo_overruns; ///< IMU FIFO overrun count.
  uint32_t sample_fifo_watermark_shorts; ///< Watermark asserted but FIFO count short.
  uint32_t sample_fifo_empty_reads; ///< FIFO read returned zero pairs.
  uint32_t sample_fifo_short_blocks; ///< Unrecoverable short superframe blocks.
  TestResult test_result;   ///< Most recent self-test result.

} BackupState;

/** Pointer to the retained backup-state region. */
extern volatile BackupState *const pState;

/********************************************************
 *  Persistent Data Formats [ read directly from flash ]
 *******************************************************/

/**
 * @enum LOGERR
 * @brief Result values returned by IMUTag log write helpers.
 */
enum LOGERR
{
  LOGWRITE_OK,    ///< Write completed successfully.
  LOGWRITE_BAT,   ///< Write refused due to battery/power policy.
  LOGWRITE_FULL,  ///< Log storage has no space for the requested write.
  LOGWRITE_ERROR  ///< Storage or flash programming failed.
};

/**
 * @brief Internal flash marker for state recovery.
 *
 * The state machine appends one marker on major state transitions so a reset
 * can recover the last known state and log cursors before resuming or aborting.
 */
typedef struct
{
  int32_t epoch;           ///< State-transition epoch in seconds.
  TagState state;          ///< State recorded at the transition.
  uint32_t internal_pages; ///< Internal checkpoint/header rows written.
  uint32_t external_pages; ///< External log pages written or recovered.
  uint16_t vdd100;         ///< Supply voltage sample in centivolts.
  int16_t temp10;          ///< Temperature sample in tenths of a degree C.
  State_Event reason;      ///< Event that caused the transition.
#if IMUTAG_STM32U3_FLASH
  uint64_t flash_padding;  ///< Padding required for STM32U3 flash rows.
} t_StateMarker __attribute__((aligned(16)));
#else
} t_StateMarker __attribute__((aligned(8)));
#endif

/** Last monitor command seen by the firmware. */
extern int32_t monitorCMD;

#if CONFIG_HAS_HIBERNATE
/** Number of state markers reserved when hibernation intervals are enabled. */
#define sEPOCH_SIZE (10 + (_TagState_MAX) + 2 * sizeof(((Config *)0)->hibernate) / sizeof(Config_Interval))
#else
/** Number of state markers reserved for non-hibernating IMUTag targets. */
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
