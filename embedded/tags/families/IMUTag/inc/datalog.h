/**
 * @file datalog.h
 * @brief IMUTagBreakout external-log record layout and log IO API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef DATALOG_H
#define DATALOG_H
#include "sensors.h"
#include "assert.h"
#include "imutag_log_format.h"

typedef t_ImuTagDataLog t_DataLog;
typedef t_ImuTagDataHeader t_DataHeader;

#if !defined(IMUTAG_STM32U3_FLASH)
#if defined(STM32U3XX) || defined(STM32U375xx) || defined(STM32U385xx) || defined(BOARD_IMUTagU375)
#define IMUTAG_STM32U3_FLASH 1
#else
#define IMUTAG_STM32U3_FLASH 0
#endif
#endif

#if IMUTAG_STM32U3_FLASH
/**
 * @brief STM32U3 internal-flash checkpoint for a sparse IMUTag NAND group.
 *
 * @details STM32U3 flash programs 16-byte rows. The first eight bytes retain
 *          the normal IMUTag data header, while the remaining row bytes anchor
 *          the first logical and physical NAND page covered by this checkpoint.
 *          Checkpoints are written every IMUTAG_CHECKPOINT_PAGES external
 *          pages, so download and recovery derive pages within a group by
 *          adding a small page delta to these anchors.
 *
 * @see embedded/tags/families/IMUTag/design/internal-header-checkpoints.md
 */
typedef struct {
  t_DataHeader header;                  ///< Timestamp and checkpoint flags.
  uint32_t external_page_logical_next;  ///< First logical NAND page covered.
  uint32_t external_page_physical_next; ///< First physical NAND page covered.
} t_InternalDataHeader __attribute__((aligned(16)));
#else
typedef t_DataHeader t_InternalDataHeader;
#endif

static_assert(sizeof(t_DataLog) == IMUTAG_PAGE_SIZE,
              "IMUTag data page must be exactly 2048 bytes!");

/** Number of external log pages returned by one raw-log ACK. */
#define DATALOG_SAMPLES 1

/** Internal flash header array placed by the linker script. */
extern t_InternalDataHeader vddHeader[];

/**
 * @brief Append a complete external log page and advance the external cursor.
 *
 * @param[in] data Complete IMUTag page image to write.
 * @return LOGWRITE_OK on success, LOGWRITE_FULL when the external flash cursor
 *         is out of space, or LOGWRITE_ERROR on storage/programming failure.
 */
extern enum LOGERR writeDataLog(t_DataLog *data);
/**
 * @brief Stage the header for the current external log page.
 *
 * @param[in] head Timestamp and temperature header for the page being built.
 * @return Log write status from the external page-cache write.
 */
extern enum LOGERR writeDataLogPageHeader(t_DataHeader *head);
/**
 * @brief Stage a page header and first superframe into the external page cache.
 *
 * @param[in] head Timestamp and temperature header for the page being built.
 * @param[in] frame First superframe in the page.
 * @return Log write status from the external page-cache write.
 */
extern enum LOGERR writeDataLogPageStart(t_DataHeader *head,
                                         const t_ImuTagSuperFrame *frame);
/**
 * @brief Stage one superframe into the active external page cache.
 *
 * @param[in] frame_index Superframe index inside the page. Must be less than
 *                        IMUTAG_SUPERFRAMES_PER_PAGE.
 * @param[in] frame Superframe data to write.
 * @return LOGWRITE_OK on success, LOGWRITE_ERROR for an invalid index or cache
 *         write failure, or LOGWRITE_FULL when the external cursor is full.
 */
extern enum LOGERR writeDataLogSuperFrame(uint16_t frame_index,
                                          const t_ImuTagSuperFrame *frame);
/**
 * @brief Commit any staged external page cache to storage.
 *
 * @return LOGWRITE_OK when no cache is active or the commit succeeds;
 *         LOGWRITE_ERROR when NAND PROGRAM_EXECUTE fails.
 */
extern enum LOGERR commitDataLogPage(void);
/**
 * @brief Report whether the current external page starts a checkpoint group.
 *
 * @return true when the current page needs a new internal checkpoint before
 *         external data is written.
 */
extern bool dataLogCheckpointDue(void);
/**
 * @brief Write the next internal-flash checkpoint/header row.
 *
 * @param[in] head Header metadata for the checkpoint or legacy per-page entry.
 * @return LOGWRITE_OK on success, LOGWRITE_FULL when the internal header
 *         region is full, or LOGWRITE_ERROR on mapping/programming failure.
 */
extern enum LOGERR writeDataHeader(t_DataHeader *head);
/**
 * @brief Recover internal and external log cursors after reset.
 *
 * @return 0 when recovery completes. Cursor state is reported through pState.
 */
extern int restoreLog(void);

#endif
