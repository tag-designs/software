/**
 * @file datalog.h
 * @brief UIUCTag log record layout and log IO API.
 * @author tag firmware authors
 * @date 2026-08-31
 *
 * @details Tag-local replacement for the BitPresTag family log header. UIUCTag
 *          stores one 8-byte record a minute: raw 24-bit pressure and
 *          temperature, then three 20-second activity counts. The canonical
 *          layout is shared with host tools through
 *          include/uiuctag_log_format.h and is not redefined here.
 *
 *          Two storage regions are involved:
 *          - External flash holds a contiguous array of 256-byte blocks, each
 *            32 records and exactly one AT25XE page. Each minute wake writes
 *            one whole record, its reading and the activity of the minute
 *            just ended, as a single 8-byte program; a record never crosses a
 *            page.
 *          - Internal flash holds one 8-byte checkpoint per external block, in
 *            the vddHeader[] array declared by the shared core persistent.c.
 *            t_DataHeader therefore has to stay a multiple of the internal
 *            flash double-word programming granularity.
 *
 * @warning The checkpoint record is the same size as the BitPresTag one it
 *          replaces but means something different, and nothing in the firmware
 *          detects the difference. Any future change to this record needs the
 *          same care: erase the log rather than assuming stale headers will be
 *          recognised as stale.
 *
 * @see     ../design/data-collection.md
 */

#ifndef DATALOG_H
#define DATALOG_H

#include "uiuctag_log_format.h"

/**
 * @brief Number of records grouped under one internal checkpoint.
 *
 * @details Named for compatibility with shared core code that reports log
 *          progress; the value is the UIUCTag block record count.
 */
#define DATALOG_SAMPLES UIUCTAG_LOG_RECORDS

/** @brief Byte stride of one external log block: one flash page. */
#define DATALOG_BLOCK_BYTES UIUCTAG_BLOCK_BYTES

/**
 * @brief Internal-flash checkpoint that anchors one external log block.
 *
 * @details Layout is the shared t_UIUCTagInternalLog: the epoch second of this
 *          block's record 0, the supply voltage at that moment in 0.01 V units,
 *          and the index of the external block it describes. The first
 *          checkpoint of a run anchors the sample grid; each later one is
 *          exactly one block period after its predecessor.
 */
typedef t_UIUCTagInternalLog t_DataHeader;

/** Internal flash checkpoint array placed by the linker script. */
extern t_DataHeader vddHeader[];

/**
 * @brief Wake the external flash for a record write.
 *
 * @details Pairs with dataLogWriteEnd().
 *
 * @post External flash is out of deep power-down until dataLogWriteEnd().
 */
extern void dataLogWriteBegin(void);

/**
 * @brief Return the external flash to deep power-down after a write burst.
 */
extern void dataLogWriteEnd(void);

/**
 * @brief Program one record of the external log.
 *
 * @details Writes the 8 bytes of @p record at @p index * 8 as one page
 *          program. Records are 8-byte aligned and 32 fill a page exactly, so
 *          the program never crosses a page boundary. Indices come from the
 *          acquisition clock, so a record is only ever programmed once;
 *          re-programming written bytes is not supported by NOR flash and is a
 *          caller error.
 *
 * @param[in] index Global record index, counting from the first record of
 *                  block 0.
 * @param[in] record Record to program.
 * @return LOGWRITE_OK on success, LOGWRITE_FULL when the record lies beyond
 *         external flash capacity (nothing is written), or LOGWRITE_ERROR
 *         when the program cycle fails.
 *
 * @pre dataLogWriteBegin() has been called.
 * @warning Blocks for the page program time.
 */
extern enum LOGERR dataLogWriteRecord(uint32_t index,
                                      const t_UIUCTagRecord *record);

/**
 * @brief Write the next internal-flash checkpoint.
 *
 * @param[in] head Checkpoint to persist.
 * @return LOGWRITE_OK on success, LOGWRITE_FULL when the persistent region is
 *         exhausted, LOGWRITE_ERROR on a flash program failure, or
 *         LOGWRITE_BAT when the recorded voltage is below the collection floor.
 *
 * @post pState->pages is incremented, so the checkpoint index of the block just
 *       opened is pState->pages - 1.
 */
extern enum LOGERR writeDataHeader(t_DataHeader *head);

/**
 * @brief Read one internal-flash checkpoint.
 *
 * @param[in] index Checkpoint index.
 * @param[out] header Destination checkpoint.
 * @return true when the index is inside the persistent region and the read
 *         succeeded. An unwritten checkpoint reads back with epoch == -1.
 */
extern bool readDataHeader(int index, t_DataHeader *header);

/**
 * @brief Recover log cursors from internal flash after reset.
 *
 * @details Counts written checkpoints into pState->pages and sets
 *          pState->external_blocks to one past the global index of the last
 *          written record, read from the newest checkpoint's block in external
 *          flash. That count is reported in the status only; the run does not
 *          resume from it. Where the next record goes is derived from the
 *          acquisition clock and the newest checkpoint, so a reset mid-block
 *          resumes at the correct slot without re-programming written records.
 *
 * @return 0 when recovery completes.
 */
extern int restoreLog(void);

#endif
