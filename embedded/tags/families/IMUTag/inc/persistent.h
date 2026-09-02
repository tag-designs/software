/**
 * @file persistent.h
 * @brief IMUTag family persistent runtime state and log metadata layout.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef PERSISTENT_H
#define PERSISTENT_H

#include <assert.h>
#include <stdbool.h>

#include "custom.h"
#include "config.h"

#if !defined(CONFIG_HAS_HIBERNATE)
/** @brief Enable hibernation-state support unless a target opts out. */
#define CONFIG_HAS_HIBERNATE 1
#endif

/** @brief Magic value marking retained BackupState contents as initialized. */
#define BACKUP_STATE_VALID_MAGIC 0x54414742U

/**
 * @brief One-shot retained marker for Stop3 wakeups that reset instead of SBF.
 */
#define TAG_SYNTHETIC_STANDBY_WAKE_MAGIC 0x53544259U

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

#if !defined(TAG_RECOVERY_TRACE)
/**
 * @brief Retain a trace of what boot reset recovery saw and decided.
 *
 * @details Reset recovery is the least observable code in the tag: it runs
 *          before the monitor thread exists, its inputs (reset cause, retained
 *          backup state, the internal-flash marker log) are all gone or
 *          rewritten by the time a host can ask, and its only other narration
 *          is debug_log_printf(), which shipped images compile out. Enabling
 *          this adds six retained words and a status line that together report
 *          which recovery branch ran and what it resolved.
 *
 * @note Off by default as a policy choice, not a cost. It once measured 995 uA
 *       idle against 6.56 uA, but that was a missing pre-sleep flash
 *       error-flag clear in the U3 power path, not this code: with
 *       tagPowerClearFlashErrorFlags() in place it measures 6.705 uA against
 *       6.716 uA with it disabled, which is within run-to-run spread. Enable
 *       it when a boot-recovery question needs answering.
 *
 * @see tagPowerClearFlashErrorFlags() in core/src/pwr-u375.c,
 *      embedded/tags/design/restart-recovery.md
 */
#define TAG_RECOVERY_TRACE 0
#endif

#if TAG_RECOVERY_TRACE
/** @name Boot recovery trace bits
 * Bit assignments for BackupState::recovery_flags. Bits 0..15 record what
 * recovery observed and which branch it took; bits 24..27 carry the
 * @ref t_resetCause it dispatched on.
 * @{
 */
/** @brief Boot entered the reset-recovery block rather than ordinary dispatch. */
#define RECOVERY_TRACE_ENTERED          (1U << 0)
/** @brief Recovery classified the reset as a monitor connect-under-reset. */
#define RECOVERY_TRACE_MONITOR          (1U << 1)
/** @brief Retained BackupState carried BACKUP_STATE_VALID_MAGIC. */
#define RECOVERY_TRACE_RETAINED_VALID   (1U << 2)
/** @brief Recovery was entered with no retained state (STATE_UNSPECIFIED). */
#define RECOVERY_TRACE_FROM_UNSPEC      (1U << 3)
/** @brief Recovery resolved a concrete state rather than defaulting to idle. */
#define RECOVERY_TRACE_CONCRETE         (1U << 4)
/** @brief Recovery adopted the retained state instead of scanning markers. */
#define RECOVERY_TRACE_ADOPTED_RETAINED (1U << 5)
/** @brief The marker scan stopped because a marker read failed (ECC/fault). */
#define RECOVERY_TRACE_MARKER_READFAIL  (1U << 6)
/** @brief The marker scan stopped on an erased entry (epoch == -1). */
#define RECOVERY_TRACE_MARKER_BLANK     (1U << 7)
/** @brief The marker scan stopped on a marker that failed validation. */
#define RECOVERY_TRACE_MARKER_INVALID   (1U << 8)
/** @brief The marker scan reached the end of the log with every entry valid. */
#define RECOVERY_TRACE_MARKER_FULL      (1U << 9)
/** @brief Boot restored the wall clock from the external RTC. */
#define RECOVERY_TRACE_CLOCK_RECOVERED  (1U << 10)
/** @brief Recovery left clockTrusted set. */
#define RECOVERY_TRACE_CLOCK_TRUSTED    (1U << 11)
/** @brief The STM32 RTC calendar was already initialized at boot. */
#define RECOVERY_TRACE_RTC_INIT_BOOT    (1U << 12)
/** @brief Shift of the t_resetCause field within recovery_flags. */
#define RECOVERY_TRACE_CAUSE_SHIFT      24U
/** @brief Mask of the t_resetCause field within recovery_flags. */
#define RECOVERY_TRACE_CAUSE_MASK       (0xFU << RECOVERY_TRACE_CAUSE_SHIFT)
/** @} */

/**
 * @def RECOVERY_TRACE_STATES
 * @brief Pack the four one-byte state values of recovery_states.
 *
 * @param entry    TagState held by pState on entry to recovery.
 * @param resolved TagState pState held once recovery finished resolving it.
 * @param markers  Number of marker-log entries the scan consumed (saturated
 *                 at 255).
 * @param reason   State_Event of the last valid marker the scan accepted.
 */
#define RECOVERY_TRACE_STATES(entry, resolved, markers, reason) \
  ((((uint32_t)(entry) & 0xFFU)) | \
   (((uint32_t)(resolved) & 0xFFU) << 8) | \
   (((uint32_t)(markers) & 0xFFU) << 16) | \
   (((uint32_t)(reason) & 0xFFU) << 24))

/**
 * @def RECOVERY_TRACE_HIST_DEPTH
 * @brief Number of resolved states retained in recovery_state_hist.
 *
 * @details The history exists because a tag can only be observed by attaching
 *          a monitor, and attaching resets the core -- so the single-slot
 *          recovery_states always describes the healthy attach boot rather than
 *          the detached boot under investigation. Entries are pushed only when
 *          the resolved state differs from the previous entry, so the run's
 *          many standby wakeups compress to the transitions that matter and
 *          eight nibbles cover a whole run.
 */
#define RECOVERY_TRACE_HIST_DEPTH 8U
#endif

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
  uint32_t synthetic_standby_wake; ///< One-shot marker for reset-after-Stop3 wake.
#if TAG_RECOVERY_TRACE
  uint32_t recovery_boots;   ///< Boots that ran reset recovery; wraps freely.
  uint32_t recovery_flags;   ///< RECOVERY_TRACE_* bits and the reset cause.
  uint32_t recovery_states;  ///< See RECOVERY_TRACE_STATES().
  uint32_t recovery_flags_seen; ///< Sticky OR of recovery_flags over all boots.
  uint32_t recovery_state_hist; ///< Eight-deep nibble history of resolved states.
  uint32_t recovery_change_flags;  ///< recovery_flags of the last state change.
  uint32_t recovery_change_states; ///< recovery_states of the last state change.
  uint32_t recovery_wipes; ///< Times boot cleanup discarded retained runtime state.
#endif

} BackupState;

/*
 * pState is a direct overlay on the backup registers -- TAMP->BKP0R on STM32U3,
 * RTC->BKP0R elsewhere -- of which both the STM32U375 and the STM32L432 provide
 * 32. Overflowing that silently aliases retained state onto whatever follows
 * the register block, with no diagnostic.
 *
 * Non-U3 targets reserve the last register for the Shutdown-wake marker; see
 * TAG_SHUTDOWN_WAKE_MARKER_SUPPORTED in core/src/pwr.c, which is enabled only
 * when TAG_STM32U3_FLASH is off.
 */
#if IMUTAG_STM32U3_FLASH
/** @brief Backup registers available to BackupState on this target. */
#define BACKUP_STATE_MAX_WORDS 32U
#else
/** @brief Backup registers available to BackupState, less the Shutdown marker. */
#define BACKUP_STATE_MAX_WORDS 31U
#endif

static_assert(sizeof(BackupState) <= BACKUP_STATE_MAX_WORDS * sizeof(uint32_t),
              "BackupState exceeds the available backup registers");

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
  /**
   * @brief Reason-scoped diagnostic detail, or 0 when the transition has none.
   *
   * @details Interpreted relative to @c reason, so each event owns its own
   *          encoding and new detail can be added without touching this
   *          record. Its purpose is to make transitions that currently reach
   *          flash as a bare State_EVENT_UNKNOWN say why: the only other
   *          narration in those paths is debug_log_printf(), which shipped
   *          images compile out because the debug module prevents standby.
   *
   *          Costs nothing to carry. recordState() already programs this
   *          record and already bzero()s it first, so 0 means "no detail" in
   *          every marker written by earlier firmware and no flash write,
   *          erase or region is added.
   *
   * @note Available on STM32U3 only, where it comes out of the padding the
   *       128-bit flash programming row requires. The STM32L4 record is 24
   *       bytes of real fields with no slack, and growing it would cost
   *       marker-log capacity and invalidate existing logs on deployed tags.
   *
   * @see tagStateMarkerDetail(), IMUTAG_INIT_FAIL_* in sensors.h
   */
  uint32_t detail;
  uint32_t flash_padding;  ///< Remaining padding for the 128-bit flash row.
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
/**
 * @brief Supply the reason-scoped detail word for the next state marker.
 *
 * @details Weak default returns 0. A family overrides it to record why a
 *          transition happened; see t_StateMarker::detail. Called with the
 *          system locked, so it must only return an already-latched value.
 *
 * @return Detail word for t_StateMarker::detail, or 0 for no detail.
 */
#if IMUTAG_STM32U3_FLASH
uint32_t tagStateMarkerDetail(void);
#endif

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
/** @brief Report whether the current incremental external erase failed. */
bool eraseExternalFailed(void);
/** @brief Erase one external log block, when supported by the target. */
void eraseExternalBlock(void);
/** @brief Report external flash capacity in bytes. */
uint32_t externalFlashSize(void);
/** @brief Report progress while external sectors are being erased. */
int externalFlashSectorsErased(void);
/** @brief Report total external sectors expected for the current erase, plus one. */
int externalFlashSectorsToErasePlusOne(void);
#endif
