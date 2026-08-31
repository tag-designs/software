/**
 * @file datalog.h
 * @brief UIUCTag log record layout and log IO API.
 * @author tag firmware authors
 * @date 2026-08-31
 *
 * @details Tag-local replacement for the BitPresTag family log header. UIUCTag
 *          stores compact float pressure/temperature samples with packed
 *          per-minute activity counts; the canonical layout is shared with host
 *          tools through include/uiuctag_log_format.h and is not redefined
 *          here.
 *
 *          Two storage regions are involved:
 *          - External flash holds a contiguous array of 288-byte blocks, each
 *            24 twelve-byte samples. Sample fields are programmed one 32-bit
 *            word at a time as they become available.
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
 * @see     ../../families/BitPresTag/design/uiuctag-data-collection.md
 */

#ifndef DATALOG_H
#define DATALOG_H

#include "uiuctag_log_format.h"

/**
 * @brief Number of samples grouped under one internal checkpoint.
 *
 * @details Named for compatibility with shared core code that reports log
 *          progress; the value is the UIUCTag block sample count.
 */
#define DATALOG_SAMPLES UIUCTAG_LOG_SAMPLES

/** @brief Byte stride of one external log block. */
#define DATALOG_BLOCK_BYTES UIUCTAG_SAMPLE_BYTES_MAX

/**
 * @brief Internal-flash checkpoint that anchors one external log block.
 *
 * @details Layout is the shared t_UIUCTagInternalLog: the epoch second of this
 *          block's slot 0, the supply voltage at that moment in 0.01 V units,
 *          and the index of the external block it describes. The first
 *          checkpoint of a run anchors the sample grid; each later one is
 *          exactly one block period after its predecessor.
 */
typedef t_UIUCTagInternalLog t_DataHeader;

/** Internal flash checkpoint array placed by the linker script. */
extern t_DataHeader vddHeader[];

/** @brief Byte offset of the pressure field within a sample. */
#define DATALOG_FIELD_PRESSURE 0u
/** @brief Byte offset of the temperature field within a sample. */
#define DATALOG_FIELD_TEMPERATURE 4u
/** @brief Byte offset of the packed activity field within a sample. */
#define DATALOG_FIELD_ACTIVITY 8u

/**
 * @brief Wake the external flash for a burst of sample-field writes.
 *
 * @details Pairs with dataLogWriteEnd(). Holding the chip awake across the two
 *          or three writes of one acquisition wake keeps the deep-power-down
 *          exit cost to once per wake instead of once per word.
 *
 * @post External flash is out of deep power-down until dataLogWriteEnd().
 */
extern void dataLogWriteBegin(void);

/**
 * @brief Return the external flash to deep power-down after a write burst.
 */
extern void dataLogWriteEnd(void);

/**
 * @brief Program one 32-bit field of one external sample slot.
 *
 * @details Writes exactly four bytes at
 *          @p sample_index * 12 + @p field_offset, then rests for
 *          UIUCTAG_WRITE_REST_MS in a low-power sleep so the storage capacitor
 *          recharges before the next program cycle. Slot addresses are derived
 *          from the acquisition clock, so a field is only ever programmed once;
 *          re-programming an already written word is not supported by NOR
 *          flash and is treated as a caller error.
 *
 * @param[in] sample_index Global sample index, counting from the first sample
 *                         of the first external block.
 * @param[in] field_offset One of the DATALOG_FIELD_* offsets.
 * @param[in] word Four bytes to program.
 * @return LOGWRITE_OK on success, LOGWRITE_FULL when the address is beyond
 *         external flash capacity, or LOGWRITE_ERROR when the program cycle
 *         fails.
 *
 * @pre dataLogWriteBegin() has been called.
 * @warning Blocks for the flash program time plus the recharge rest.
 */
extern enum LOGERR dataLogWriteField(uint32_t sample_index,
                                     uint32_t field_offset,
                                     const void *word);

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
 *          pState->external_blocks to the same value, since one checkpoint
 *          always corresponds to exactly one external block. Progress within
 *          the current block is not recovered here: it is derived from the
 *          acquisition clock and the newest checkpoint, so a reset mid-block
 *          resumes at the correct slot without re-programming written fields.
 *
 * @return 0 when recovery completes.
 */
extern int restoreLog(void);

#endif
