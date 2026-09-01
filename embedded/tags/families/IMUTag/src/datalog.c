/**
 * @file datalog.c
 * @brief IMUTag family log storage, erase support, and monitor ACK export.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "app.h"
#include "datalog.h"
#include "debug_log.h"
#include "flash_internal.h"
#include <stdint.h>
#include <string.h>
#include <tag.pb.h>
#include "devices.h"
#include "persistent.h"

#if (defined(TAG_FLASH_GD5F1GQ5RE) && TAG_FLASH_GD5F1GQ5RE) || \
    (defined(TAG_FLASH_GD5F2GM7RE) && TAG_FLASH_GD5F2GM7RE)
#include "storage_gd5f.h"
/** @brief Enable sparse internal checkpoints for NAND-backed IMUTag logs. */
#define IMUTAG_NAND_CHECKPOINTS 1
#else
/** @brief Use one internal header per external page on legacy storage. */
#define IMUTAG_NAND_CHECKPOINTS 0
#endif

/** Number of bytes exported by one raw-log acknowledgement. */
const int databuf_size = DATALOG_SAMPLES * sizeof(t_DataLog);
/** Raw-log acknowledgement staging buffer. */
static t_DataLog databuf NOINIT;
/** True when a NAND page-program cache is active and needs commit. */
static bool datalog_page_cache_active;

static_assert(sizeof(((IMUTagRawLog*)0)->samples.bytes) == DATALOG_SAMPLES * sizeof(t_DataLog),
              "nanopb IMUTagRawLog.samples buffer size in options is out of sync with datalog page size!");
/**
 * Number of external erase sectors completed by the incremental erase path.
 *
 * Deliberately NOT declared NOINIT. It is reported to the host in every Status
 * reply through externalFlashSectorsErased(), so it must read zero before any
 * erase has run; in .ram0 it instead returned whatever the region happened to
 * hold, observed as large negative values that differed between polls.
 *
 * Retaining it across a reset would achieve nothing: its gate,
 * erase_external_active, is ordinary .bss and is cleared at every boot, so a
 * surviving count could not be resumed, and eraseExternalStart() zeroes this
 * anyway on entry to each erase. All four companion variables below are
 * likewise ordinary statics.
 */
static volatile int sectors_erased;
/** Active external erase-sector size in bytes. */
static uint32_t erase_sector_size;
/** Total external erase sectors selected for the current erase. */
static uint32_t erase_sector_total;
/** True while an incremental external erase is active. */
static bool erase_external_active;
/** Sticky failure flag for the current incremental external erase. */
static bool erase_external_failed;
/** Last internal-header flash program failure observed by writeDataHeader(). */
static uint32_t last_internal_header_flash_error;

extern int encode_ack(void);

#if defined(LOG_ACK_MEASURE_LINE)
/** Tracks whether LOG_ACK_MEASURE_LINE has been configured as an output. */
static bool log_ack_measure_line_initialized;

/** @brief Assert the optional raw-log acknowledgement measurement line. */
static void logAckMeasureBegin(void)
{
  if (!log_ack_measure_line_initialized) {
    palClearLine(LOG_ACK_MEASURE_LINE);
    palSetLineMode(LOG_ACK_MEASURE_LINE, PAL_MODE_OUTPUT_PUSHPULL);
    log_ack_measure_line_initialized = true;
  }
  palSetLine(LOG_ACK_MEASURE_LINE);
}

/** @brief Deassert the optional raw-log acknowledgement measurement line. */
static void logAckMeasureEnd(void)
{
  palClearLine(LOG_ACK_MEASURE_LINE);
}
#else
/** @brief No-op raw-log acknowledgement measurement hook. */
#define logAckMeasureBegin() do { } while (0)
/** @brief No-op raw-log acknowledgement measurement hook. */
#define logAckMeasureEnd() do { } while (0)
#endif

/**
 * @brief Estimate how many external erase sectors contain dirty log data.
 *
 * @details Recovery restores the external page cursor before erase begins.
 *          The estimate erases one extra page span so a partially programmed
 *          page from an interrupted write is also cleared.
 *
 * @return Number of external sectors to erase, capped at device capacity.
 */
static uint32_t dirtyExternalSectors(void)
{
  const uint32_t sector_size = tagStorageSectorSize(TAG_EXTERNAL_FLASH);
  const uint32_t sector_count = tagStorageSectorCount(TAG_EXTERNAL_FLASH);
  /*
   * Reset calls restoreLog() before eraseExternal(), so pState->external_blocks
   * is the recovered external page cursor. Incremental page writes can leave
   * one uncommitted page partially programmed after a committed page, so erase
   * one extra page span when converting a non-empty log to dirty sectors.
   */
  const uint64_t dirty_bytes = ((uint64_t)pState->external_blocks + 1U) *
                               (uint64_t)databuf_size;
  uint32_t dirty_sectors;

  if (sector_size == 0U || sector_count == 0U) {
    return 0;
  }
  if (pState->external_blocks == 0U) {
    return 0;
  }

  dirty_sectors = (uint32_t)((dirty_bytes + sector_size - 1U) / sector_size);
  if (dirty_sectors > sector_count) {
    dirty_sectors = sector_count;
  }
  return dirty_sectors;
}

/**
 * @brief Read one raw internal header/checkpoint slot from STM32 flash.
 *
 * @param[in] index Internal header slot index.
 * @param[out] slot Destination for the complete internal row.
 * @return true when the slot address is inside the persistent header region
 *         and the STM32 flash read passes ECC validation.
 */
static bool readInternalHeader(int index, t_InternalDataHeader *slot)
{
  uint32_t end = (uint32_t)&__persistent_end__;

  if (index < 0 || slot == NULL)
    return false;

  uint32_t address = (uint32_t)&vddHeader[index];
  if ((address + sizeof(vddHeader[index])) > end)
    return false;

  return FLASH_Read_Checked(&vddHeader[index], slot, sizeof(*slot)) == 0;
}

#if !IMUTAG_NAND_CHECKPOINTS
/**
 * @brief Read one legacy per-page data header from the internal header stream.
 *
 * @param[in] index Internal header slot index.
 * @param[out] header Destination for the extracted page header.
 * @return true when the slot can be read and converted to a t_DataHeader.
 */
static bool readDataHeader(int index, t_DataHeader *header)
{
  t_InternalDataHeader slot;

  if (header == NULL)
    return false;
  if (!readInternalHeader(index, &slot))
    return false;
#if IMUTAG_STM32U3_FLASH
  *header = slot.header;
#else
  *header = slot;
#endif
  return true;
}
#endif

/**
 * @brief Test whether an internal header slot is still in the erased state.
 *
 * @param[in] slot Internal header row previously read from STM32 flash.
 * @return true when the slot is absent or its embedded epoch is erased.
 */
static bool internalHeaderErased(const t_InternalDataHeader *slot)
{
#if IMUTAG_STM32U3_FLASH
  return slot == NULL || slot->header.epoch == -1;
#else
  return slot == NULL || slot->epoch == -1;
#endif
}

/**
 * @brief Erase the external data log and reset log progress.
 */
void eraseExternal()
{
  eraseExternalStart();
  while (eraseExternalNextSector())
    chThdYield();
  eraseExternalFinish();
}

/** Leading bytes of a sector examined to decide whether it holds data. */
#define IMUTAG_ERASE_PROBE_BYTES 16U

/**
 * @brief Report whether an external sector reads as blank.
 *
 * @details Log pages are written contiguously from sector zero, so the first
 *          blank sector marks the end of everything ever written. Reading the
 *          device is the only trustworthy way to find that boundary: the
 *          alternative, deriving it from pState->external_blocks, fails exactly
 *          when it matters most, because a power loss that leaves data behind
 *          also destroys the retained cursor.
 *
 * @param[in] address Byte address of the sector to probe.
 * @return true when the leading bytes are all 0xFF. A sector that cannot be
 *         read is reported as non-blank, so an unreadable sector is erased
 *         rather than silently ending the sweep early.
 */
static bool externalSectorIsBlank(uint32_t address)
{
  uint8_t probe[IMUTAG_ERASE_PROBE_BYTES];

  memset(probe, 0x00, sizeof(probe));
  tagStorageRead(TAG_EXTERNAL_FLASH, address, probe, (int)sizeof(probe));
  for (size_t i = 0U; i < sizeof(probe); i++) {
    if (probe[i] != 0xFFU)
      return false;
  }
  return true;
}

/**
 * @brief Find how many external sectors have ever been written.
 *
 * @details Binary searches for the highest sector that still holds data and
 *          returns one past it. Nothing is ever written above the highest write,
 *          so the blank/dirty boundary is monotonic from the top and the search
 *          is sound.
 *
 *          Deliberately searches for the last dirty sector rather than the first
 *          blank one. Those differ after an interrupted erase, which leaves a
 *          blank region below sectors that still hold data: a first-blank search
 *          would stop at sector zero and leave the rest behind, which is exactly
 *          the state a tag is in when a power loss interrupts a reset.
 *
 *          Reading the device is the only trustworthy source. Deriving the
 *          extent from pState->external_blocks fails precisely when it matters,
 *          because the power loss that leaves data behind also destroys the
 *          retained cursor.
 *
 * @param[in] sector_size Sector size in bytes.
 * @param[in] sector_count Total sectors in the logical address space.
 * @return Number of sectors to sweep, zero when the device reads blank.
 */
static uint32_t lastDirtyExternalSector(uint32_t sector_size,
                                        uint32_t sector_count)
{
  uint32_t lo = 0U;
  uint32_t hi = sector_count;
  uint32_t last_dirty = 0U;
  bool any_dirty = false;

  while (lo < hi) {
    const uint32_t mid = lo + ((hi - lo) / 2U);

    if (externalSectorIsBlank(mid * sector_size)) {
      hi = mid;
    } else {
      last_dirty = mid;
      any_dirty = true;
      lo = mid + 1U;
    }
  }
  return any_dirty ? (last_dirty + 1U) : 0U;
}

void eraseExternalStart(void)
{
  const uint32_t sector_size = tagStorageSectorSize(TAG_EXTERNAL_FLASH);
  const uint32_t sector_count = (uint32_t)tagStorageSectorCount(TAG_EXTERNAL_FLASH);
  /*
   * Sweep from sector zero up to the last sector that holds data, located by
   * reading the device rather than by trusting a retained cursor.
   *
   * NEVER iterate physical blocks here. The logical map is built by
   * gd5fScanFactoryBadBlocks() and therefore contains only good blocks, which is
   * what keeps this sweep away from factory bad-block markers. Erasing a bad
   * block destroys its marker permanently, and a device whose markers are gone
   * can no longer distinguish bad media from good.
   */
  erase_sector_size = sector_size;
  erase_sector_total = 0U;
  sectors_erased = 0;
  erase_external_active = false;
  erase_external_failed = false;

  if (sector_size == 0U || sector_count == 0U) {
    return;
  }

  /*
   * Wake before probing, and only then search.
   *
   * lastDirtyExternalSector() reads the device. A NAND still asleep, or still in
   * deep power-down, does not return 0xFF, so probing it first makes every
   * sector look dirty and commits the tag to erasing the entire device. Ordering
   * matters here in a way it did not when the extent came from a retained
   * counter and this function touched no hardware before waking.
   */
  tagStorageWake(TAG_EXTERNAL_FLASH);

  erase_sector_total = lastDirtyExternalSector(sector_size, sector_count);
  if (erase_sector_total == 0U) {
    tagStorageSleep(TAG_EXTERNAL_FLASH);
    return;
  }

  erase_external_active = true;
}

bool eraseExternalNextSector(void)
{
  if (!erase_external_active)
    return false;

  if ((uint32_t)sectors_erased < erase_sector_total) {
    uint32_t sector = (uint32_t)sectors_erased;
    uint32_t address = sector * erase_sector_size;

    /*
     * Skip sectors that are already blank rather than ending the sweep: an
     * interrupted erase leaves blank sectors below ones that still hold data,
     * and stopping here would strand them. Skipping also spares a flash cycle.
     */
    if (externalSectorIsBlank(address)) {
      sectors_erased++;
      return (uint32_t)sectors_erased < erase_sector_total;
    }

    if (!tagStorageSectorErase(TAG_EXTERNAL_FLASH, address)) {
      erase_external_failed = true;
      debug_log_printf(
          "IMUTag erase: external sector %u address 0x%x failed\r\n",
          (unsigned)sector, (unsigned)address);
      return false;
    }
    sectors_erased++;
  }

  return (uint32_t)sectors_erased < erase_sector_total;
}

void eraseExternalFinish(void)
{
  if (erase_external_active)
    tagStorageSleep(TAG_EXTERNAL_FLASH);
  erase_external_active = false;
  if (!erase_external_failed)
    pState->external_blocks = 0;
  sectors_erased = 0;
}

bool eraseExternalFailed(void)
{
  return erase_external_failed;
}

/**
 * @brief Return external flash capacity in bytes.
 *
 * @return External storage capacity.
 */
uint32_t externalFlashSize(void)
{
  return tagStorageSectorSize(TAG_EXTERNAL_FLASH) *
         tagStorageSectorCount(TAG_EXTERNAL_FLASH);
}

/**
 * @brief Report current erase progress for monitor polling.
 *
 * @return Number of sectors processed in the active erase.
 */
int externalFlashSectorsErased(void)
{
  return sectors_erased;
}

/**
 * @brief Report erase progress denominator for monitor polling.
 *
 * @details An estimate only, and display-only: the erase sweep now stops at the
 *          first blank sector rather than at a computed count, so the work
 *          actually done can exceed this when the retained cursor did not
 *          survive the power loss that left the data behind. Nothing controls
 *          on this value.
 *
 * @return Number of external sectors expected during erase, plus one so zero
 *         remains distinguishable from an unknown or unavailable total.
 */
int externalFlashSectorsToErasePlusOne(void)
{
  return (int)dirtyExternalSectors() + 1;
}

/**
 * @brief Report whether the current external page starts a checkpoint group.
 *
 * @details NAND-backed IMUTag variants use sparse STM32 internal checkpoints:
 *          one checkpoint covers IMUTAG_CHECKPOINT_PAGES external NAND pages.
 *          If a reset occurs after a checkpoint but before any page in that
 *          group is committed, this function suppresses a duplicate checkpoint
 *          for the same logical group after recovery.
 *
 * @return true when the next retained page should first write an internal
 *         checkpoint, false when it should only append external page data.
 */
bool dataLogCheckpointDue(void)
{
#if IMUTAG_NAND_CHECKPOINTS
  t_InternalDataHeader slot;

  if ((pState->external_blocks % IMUTAG_CHECKPOINT_PAGES) != 0U)
    return false;
  if (pState->pages == 0U)
    return true;
  if (!readInternalHeader((int)pState->pages - 1, &slot) ||
      internalHeaderErased(&slot))
    return true;
  return slot.external_page_logical_next != pState->external_blocks;
#else
  return true;
#endif
}

/**
 * @brief Resolve an IMUTag logical external page to a physical page address.
 *
 * @param[in] logical_page External log page index used by firmware and host.
 * @param[out] physical_page Physical NAND page selected by the active map.
 * @return true when the page can be mapped, false if the NAND map is absent,
 *         invalid, or the page is out of range.
 */
static bool dataLogPhysicalPageForLogical(uint32_t logical_page,
                                          uint32_t *physical_page)
{
  if (physical_page == NULL)
    return false;
#if IMUTAG_NAND_CHECKPOINTS
  return gd5fMapLogicalPage(logical_page, physical_page);
#else
  *physical_page = logical_page;
  return true;
#endif
}

/**
 * @brief Test whether a fetched external page contains a programmed log header.
 *
 * @param[in] page External log page image read from storage.
 * @return true when the page has a non-erased timestamp header.
 */
static bool dataLogExternalPageValid(const t_DataLog *page)
{
  if (page == NULL)
    return false;
  return page->slow_data.epoch != -1;
}

#if IMUTAG_NAND_CHECKPOINTS
/**
 * @brief Read and validate one physical NAND page as an IMUTag log page.
 *
 * @param[in] physical_page Physical NAND page index.
 * @param[out] page Destination for the complete external log page.
 * @param[out] read_result Optional raw NAND read/ECC status.
 * @return true when the page read completed with clean or corrected ECC and
 *         the embedded IMUTag page header is programmed.
 */
static bool dataLogReadPhysicalPage(uint32_t physical_page,
                                    t_DataLog *page,
                                    gd5f_page_read_result_t *read_result)
{
  gd5f_page_read_result_t result;

  if (read_result != NULL)
    *read_result = GD5F_PAGE_READ_ERROR;
  if (page == NULL)
    return false;

  tagStorageWake(TAG_EXTERNAL_FLASH);
  result = gd5fReadPhysicalPage(TAG_EXTERNAL_FLASH, physical_page,
                                (uint8_t *)page, sizeof(*page));
  tagStorageSleep(TAG_EXTERNAL_FLASH);

  if (read_result != NULL)
    *read_result = result;
  return (result == GD5F_PAGE_READ_OK ||
          result == GD5F_PAGE_READ_ECC_CORRECTED) &&
         dataLogExternalPageValid(page);
}

/**
 * @brief Read and validate one logical NAND page as an IMUTag log page.
 *
 * @param[in] logical_page External log page index.
 * @param[out] page Destination for the complete external log page.
 * @param[out] read_result Optional raw NAND read/ECC status.
 * @return true when mapping succeeds and the mapped physical page is readable
 *         as a programmed IMUTag data page.
 */
static bool dataLogReadLogicalPage(uint32_t logical_page,
                                   t_DataLog *page,
                                   gd5f_page_read_result_t *read_result)
{
  uint32_t physical_page;

  if (!dataLogPhysicalPageForLogical(logical_page, &physical_page))
    return false;
  return dataLogReadPhysicalPage(physical_page, page, read_result);
}

/**
 * @brief Find the sparse internal checkpoint covering a requested log page.
 *
 * @details Checkpoints are cadence-aligned but may not have a one-to-one index
 *          relationship with external pages after partial-group recovery. This
 *          searches backward from the expected cadence slot until it finds a
 *          checkpoint whose logical anchor covers @p requested_page.
 *
 * @param[in] requested_page External logical page requested by the host.
 * @param[out] slot Populated with the covering internal checkpoint.
 * @return true when a valid checkpoint covers @p requested_page.
 */
static bool readCheckpointForPage(uint32_t requested_page,
                                  t_InternalDataHeader *slot)
{
  uint32_t count = pState->pages;
  uint32_t candidate;

  if (slot == NULL || count == 0U)
    return false;

  candidate = requested_page / IMUTAG_CHECKPOINT_PAGES;
  if (candidate >= count)
    candidate = count - 1U;

  for (;;) {
    uint32_t logical_next;

    if (!readInternalHeader((int)candidate, slot) ||
        internalHeaderErased(slot))
      return false;
    logical_next = slot->external_page_logical_next;
    if (logical_next <= requested_page &&
        (requested_page - logical_next) < IMUTAG_CHECKPOINT_PAGES)
      return true;
    if (candidate == 0U)
      break;
    candidate--;
  }
  return false;
}
#endif

/**
 * @brief Recover persistent log cursors from internal flash headers.
 *
 * @details For NAND-backed IMUTag variants, the last internal header is a
 *          sparse checkpoint for an 8-page external group. Recovery scans that
 *          group until the first erased, invalid, or unreadable page and
 *          resumes at that logical page. If collection was active, the next
 *          regular checkpoint records IMUTAG_HEADER_RESTART_RECOVERY.
 *
 * @return 0 when recovery completes.
 */
int restoreLog(void)
{
  int i;
  t_InternalDataHeader slot;
  for (i = 0; readInternalHeader(i, &slot); i++)
  {
    if (internalHeaderErased(&slot))
      break;
  }
  pState->pages = i;
  pState->cycle_count = pState->pages;
#if IMUTAG_NAND_CHECKPOINTS
  pState->external_blocks = 0;
  if (i > 0 && readInternalHeader(i - 1, &slot) &&
      !internalHeaderErased(&slot)) {
    uint32_t group_start = slot.external_page_logical_next;
    uint32_t group_limit = group_start + IMUTAG_CHECKPOINT_PAGES;
    uint32_t page;

    pState->external_blocks = group_start;
    for (page = group_start; page < group_limit; page++) {
      gd5f_page_read_result_t read_result = GD5F_PAGE_READ_ERROR;

      if (!dataLogReadLogicalPage(page, &databuf, &read_result)) {
        if (read_result == GD5F_PAGE_READ_ECC_UNCORRECTABLE) {
          debug_log_printf(
              "IMUTag restore: NAND page %u has uncorrectable ECC\r\n",
              (unsigned)page);
        }
        break;
      }
      pState->external_blocks = page + 1U;
    }
    if (pState->state == TagState_RUNNING ||
        pState->state == TagState_HIBERNATING ||
        pState->state == TagState_CONFIGURED) {
      pState->checkpoint_flags_pending |= IMUTAG_HEADER_RESTART_RECOVERY;
    }
  }
#else
  pState->external_blocks = pState->pages * DATALOG_SAMPLES;
#endif
  datalog_page_cache_active = false;
  return 0;
}

static enum LOGERR writeDataLogImmediate(uint32_t page_offset,
                                         const void *data,
                                         size_t size)
{
  int cnt = (int)size;
  int addr = (int)(pState->external_blocks * sizeof(t_DataLog) + page_offset);

  tagStorageWake(TAG_EXTERNAL_FLASH);
  bool ok = tagStorageWrite(TAG_EXTERNAL_FLASH, addr, (uint8_t *) data, &cnt);
  tagStorageSleep(TAG_EXTERNAL_FLASH);

  if (!ok || cnt != (int)size)
  {
    return LOGWRITE_ERROR;
  }

  return LOGWRITE_OK;
}

static enum LOGERR writeDataLogCache(uint32_t page_offset,
                                     const void *data,
                                     size_t size)
{
  uint32_t addr =
      (uint32_t)(pState->external_blocks * sizeof(t_DataLog) + page_offset);
  bool ok;

  tagStorageWake(TAG_EXTERNAL_FLASH);
  if (page_offset == 0U && !datalog_page_cache_active) {
    ok = tagStorageProgramLoad(TAG_EXTERNAL_FLASH, addr, data, (int)size);
    if (ok) {
      datalog_page_cache_active = true;
    }
  } else if (datalog_page_cache_active) {
    ok = tagStorageProgramLoadRandom(TAG_EXTERNAL_FLASH, addr, data,
                                     (int)size);
  } else {
    ok = false;
  }
  tagStorageSleep(TAG_EXTERNAL_FLASH);

  return ok ? LOGWRITE_OK : LOGWRITE_ERROR;
}

static enum LOGERR writeDataLogBytes(uint32_t page_offset,
                                     const void *data,
                                     size_t size)
{
  uint32_t flash_capacity = tagStorageSectorSize(TAG_EXTERNAL_FLASH) *
                            tagStorageSectorCount(TAG_EXTERNAL_FLASH);

  if (data == NULL || size == 0U ||
      (page_offset + size) > sizeof(t_DataLog)) {
    return LOGWRITE_ERROR;
  }

  if ((pState->external_blocks + 1U) > flash_capacity / sizeof(t_DataLog))
  {
    return LOGWRITE_FULL;
  }

  if (tagStorageSupportsProgramCache(TAG_EXTERNAL_FLASH) &&
      page_offset == 0U && !datalog_page_cache_active) {
    enum LOGERR cache_err = writeDataLogCache(page_offset, data, size);
    if (cache_err == LOGWRITE_OK) {
      return LOGWRITE_OK;
    }
  } else if (datalog_page_cache_active) {
    return writeDataLogCache(page_offset, data, size);
  }

  return writeDataLogImmediate(page_offset, data, size);
}

/* Public API contract documented in datalog.h. */
enum LOGERR writeDataLog(t_DataLog *data)
{
  enum LOGERR err = writeDataLogBytes(0U, data, sizeof(*data));

  if (err != LOGWRITE_OK) {
    return err;
  }
  return commitDataLogPage();
}

enum LOGERR writeDataLogPageHeader(t_DataHeader *head)
{
  return writeDataLogBytes(0U, head, sizeof(*head));
}

typedef struct __attribute__((packed)) {
  t_DataHeader header;
  t_ImuTagSuperFrame frame;
} t_ImuTagPageStart;

static_assert(sizeof(t_ImuTagPageStart) ==
                sizeof(t_DataHeader) + sizeof(t_ImuTagSuperFrame),
              "IMUTag page-start write must be tightly packed");

enum LOGERR writeDataLogPageStart(t_DataHeader *head,
                                  const t_ImuTagSuperFrame *frame)
{
  t_ImuTagPageStart page_start;

  if (head == NULL || frame == NULL) {
    return LOGWRITE_ERROR;
  }

  page_start.header = *head;
  page_start.frame = *frame;
  return writeDataLogBytes(0U, &page_start, sizeof(page_start));
}

enum LOGERR writeDataLogSuperFrame(uint16_t frame_index,
                                   const t_ImuTagSuperFrame *frame)
{
  uint32_t page_offset;

  if (frame_index >= IMUTAG_SUPERFRAMES_PER_PAGE) {
    return LOGWRITE_ERROR;
  }

  page_offset = sizeof(t_ImuTagPageHeader) +
                (uint32_t)frame_index * sizeof(t_ImuTagSuperFrame);
  return writeDataLogBytes(page_offset, frame, sizeof(*frame));
}

enum LOGERR commitDataLogPage(void)
{
  uint32_t addr = pState->external_blocks * sizeof(t_DataLog);
  bool ok;

  if (!datalog_page_cache_active) {
    return LOGWRITE_OK;
  }

  tagStorageWake(TAG_EXTERNAL_FLASH);
  ok = tagStorageProgramExecute(TAG_EXTERNAL_FLASH, addr);
  tagStorageSleep(TAG_EXTERNAL_FLASH);

  if (!ok) {
    datalog_page_cache_active = false;
    return LOGWRITE_ERROR;
  }

  datalog_page_cache_active = false;
  return LOGWRITE_OK;
}

/**
 * @brief Persist an internal flash checkpoint for the next external log page.
 *
 * @details On STM32U3 NAND builds, this writes the ordinary page header plus
 *          logical and physical page anchors for the current checkpoint group.
 *          The caller is responsible for invoking this only at cadence
 *          boundaries before external page data is programmed.
 *
 * @param[in] head Header to write.
 * @return LOGWRITE_OK on success, LOGWRITE_FULL when the internal header
 *         region is full, or LOGWRITE_ERROR when page mapping or STM32 flash
 *         programming fails.
 */
extern enum LOGERR writeDataHeader(t_DataHeader *head)
{
  uint32_t flashend = (uint32_t)&__persistent_end__;
  t_InternalDataHeader slot;
  uint32_t header_page = pState->pages;
  uint32_t *writeptr = (uint32_t *)&vddHeader[header_page];
  uint32_t first_flasherr = 0;
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
  uint32_t retries = 0;
#endif

  memset(&slot, 0xff, sizeof(slot));
#if IMUTAG_STM32U3_FLASH
  slot.header = *head;
  slot.external_page_logical_next = pState->external_blocks;
  if (!dataLogPhysicalPageForLogical(pState->external_blocks,
                                     &slot.external_page_physical_next)) {
    last_internal_header_flash_error = UINT32_MAX;
    return LOGWRITE_ERROR;
  }
#else
  slot = *head;
#endif

  // See if the log file is full before programming into the configuration tail.
  if ((((uint32_t)writeptr) + sizeof(slot)) > flashend) {
    last_internal_header_flash_error = UINT32_MAX;
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
    pState->header_status = LOGWRITE_FULL;
    pState->header_flasherr = 0;
    pState->header_page = header_page;
    pState->header_addr = (uint32_t)writeptr;
    pState->header_retries = 0;
#endif
    return LOGWRITE_FULL;
  }

  chSysLock();
  FLASH_Unlock();
  uint32_t flasherr =
      FLASH_Program_Array(writeptr, (uint32_t *)&slot, sizeof(slot) / 4);
  first_flasherr = flasherr;
  if (flasherr) {
    last_internal_header_flash_error = first_flasherr ? first_flasherr : flasherr;
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
    retries++;
#endif
    flasherr =
        FLASH_Program_Array(writeptr, (uint32_t *)&slot, sizeof(slot) / 4);
  }
  FLASH_Lock();
  FLASH_Flush_Data_Cache();
  chSysUnlock();

  // See if there is still energy to continue

  if (flasherr) {
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
    pState->header_status = LOGWRITE_ERROR;
    pState->header_flasherr = first_flasherr ? first_flasherr : flasherr;
    pState->header_page = header_page;
    pState->header_addr = (uint32_t)writeptr;
    pState->header_retries = retries;
#endif
    return LOGWRITE_ERROR;
  }
  else {
    last_internal_header_flash_error = 0U;
    pState->pages = header_page + 1U;
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
    pState->header_status = LOGWRITE_OK;
    pState->header_flasherr = first_flasherr;
    pState->header_page = header_page;
    pState->header_addr = (uint32_t)writeptr;
    pState->header_retries = retries;
#endif
    return LOGWRITE_OK;
  }
}

uint32_t dataLogLastInternalHeaderFlashError(void)
{
  return last_internal_header_flash_error;
}

//
// Generate monitor ack for data log request
//   Executed by the monitor thread
//

/**
 * @brief Populate and encode a monitor ACK for one IMUTag log page.
 *
 * @details NAND builds use the sparse checkpoint stream to resolve the
 *          requested logical page to a physical NAND page. Missing, erased, or
 *          ECC-failed pages are returned as Ack_Err_NODATA so host downloaders
 *          can skip the hole and continue until external_data_count is reached.
 *
 * @param[in] index Log page index to export.
 * @param[out] ack ACK message to fill.
 * @return Encoded ACK length.
 */
int data_logAck(int index, Ack *ack)
{
  int ret;

  chThdSetPriority(HIGHPRIO);

  ack->err = Ack_Err_NODATA;
  ack->which_payload = 0;

  if (index >= 0 && (uint32_t)index < pState->external_blocks &&
      (((uint64_t)index + 1U) * sizeof(databuf) <=
       (uint64_t)externalFlashSize()))
  {
    t_DataHeader header;
    bool page_valid = false;

    ack->which_payload = Ack_imu_raw_data_log_tag;
    IMUTagRawLog *log = &ack->payload.imu_raw_data_log;

#if IMUTAG_NAND_CHECKPOINTS
    t_InternalDataHeader checkpoint;
    uint32_t delta;
    uint32_t physical_page;
    gd5f_page_read_result_t read_result = GD5F_PAGE_READ_ERROR;

    if (readCheckpointForPage((uint32_t)index, &checkpoint)) {
      delta = (uint32_t)index - checkpoint.external_page_logical_next;
      physical_page = checkpoint.external_page_physical_next + delta;
      page_valid = dataLogReadPhysicalPage(
          physical_page, (t_DataLog *)log->samples.bytes, &read_result);
      header = checkpoint.header;
      if ((uint32_t)index != checkpoint.external_page_logical_next) {
        header.millis &= IMUTAG_HEADER_MILLIS_MASK;
      }
      if (!page_valid) {
        debug_log_printf(
            "IMUTag download: page %u physical %u unavailable status=%u\r\n",
            (unsigned)index, (unsigned)physical_page,
            (unsigned)read_result);
      }
    }
#else
    uint32_t byte_offset = sizeof(databuf) * (uint32_t)index;

    if (readDataHeader(index, &header) && header.epoch != -1) {
      tagStorageWake(TAG_EXTERNAL_FLASH);
      tagStorageRead(TAG_EXTERNAL_FLASH, (uint32_t)byte_offset,
                     (uint8_t *)log->samples.bytes, databuf_size);
      tagStorageSleep(TAG_EXTERNAL_FLASH);
      page_valid = dataLogExternalPageValid(
          (const t_DataLog *)log->samples.bytes);
    }
#endif

    if (page_valid) {
      const t_DataLog *page = (const t_DataLog *)log->samples.bytes;
      uint16_t checkpoint_flags =
          (uint16_t)(header.millis & (uint16_t)~IMUTAG_HEADER_MILLIS_MASK);

      log->epoch = page->slow_data.epoch;
      log->millisecond =
        (page->slow_data.millis & IMUTAG_HEADER_MILLIS_MASK) |
        checkpoint_flags;
      log->temperature =
        page->slow_data.rawtemp * (float)IMUTAG_PRESSURE_TEMPERATURE_C_PER_LSB;
      log->samples.size = sizeof(t_DataLog);
      ack->err = Ack_Err_OK;
    } else {
      ack->which_payload = 0;
    }
  }

  // encode the ack and return
  logAckMeasureBegin();
  ret = encode_ack();
  logAckMeasureEnd();
  chThdSetPriority(NORMALPRIO);
  return ret;
}
