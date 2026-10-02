/**
 * @file    loader_flash.h
 * @brief   Part-driver interface for external loaders.
 *
 * @details One implementation per flash part (for example at25xe_loader.c).
 *          Offsets are flash byte offsets, not programmer addresses; the entry
 *          points in loader_entry.c translate and bounds-check before calling
 *          in.
 *
 *          Reading never changes the part's state beyond waking it. Erasing
 *          and programming exist only when the image is built with
 *          LOADER_ALLOW_WRITE=1: the read-only forensic image does not contain
 *          the code at all.
 */

#ifndef LOADER_FLASH_H
#define LOADER_FLASH_H

#include "loader.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief   Wake the part and confirm its identity.
 *
 * @details Reads status but never writes it: block-protect bits and any
 *          other status found on a returned tag are left as found.
 *
 * @pre     loaderSpiInit() has configured ::loaderFlashBus.
 * @return  true when the part answered with the expected JEDEC identity.
 */
bool loaderFlashProbe(void);

/** @brief Total array size in bytes. */
uint32_t loaderFlashSize(void);

/** @brief Erase granularity in bytes, matching StorageInfo's sector size. */
uint32_t loaderFlashSectorSize(void);

/**
 * @brief   Read bytes from the array.
 *
 * @param[in]  offset   Flash byte offset.
 * @param[out] buf      Destination.
 * @param[in]  n        Byte count; offset + n must not exceed the array.
 * @return  false on an SPI timeout.
 */
bool loaderFlashRead(uint32_t offset, uint8_t *buf, uint32_t n);

/**
 * @brief   Report the part's identity and status as found.
 *
 * @details The JEDEC ID is the one read by the last loaderFlashProbe(); the
 *          status register is read now and never written, so block-protect
 *          bits found on a returned tag are reported, not changed.
 *
 * @param[out] jedec  JEDEC ID, manufacturer in bits 23:16; 0 if never probed.
 * @param[out] sr1    Status register 1.
 * @return  false on an SPI timeout reading the status register.
 * @pre     loaderFlashProbe() has run.
 */
bool loaderFlashIdentity(uint32_t *jedec, uint8_t *sr1);

#if defined(LOADER_FLASH_PAGED) && LOADER_FLASH_PAGED
/**
 * @name    Paged parts (SPI NAND)
 * @details Built when the target defines LOADER_FLASH_PAGED=1. Serve() then
 *          answers LOADER_CMD_READ_PAGE.
 * @{
 */
/** @brief Bytes per page, data plus spare. */
uint32_t loaderFlashPageBytes(void);

/** @brief Number of pages in the part. */
uint32_t loaderFlashPageCount(void);

/**
 * @brief   The part's configuration as loaderFlashProbe() found it, packed
 *          as LOADER_DETAIL_FOUND describes.
 */
uint32_t loaderFlashFound(void);

/**
 * @brief   Read one whole page, data and spare.
 *
 * @param[in]  page     Physical page index.
 * @param[in]  raw      true for ECC off (bytes as stored), false for on-die ECC.
 * @param[out] buf      loaderFlashPageBytes() bytes.
 * @param[out] status   The part's status after the read (NAND: C0h).
 * @param[out] status2  Its second status register (NAND: F0h).
 * @return  false on a range error, an SPI timeout, a part that stopped
 *          answering, or a raw read refused because the configuration
 *          found does not allow the mode change safely.
 */
bool loaderFlashReadPage(uint32_t page, bool raw, uint8_t *buf,
                         uint8_t *status, uint8_t *status2);

/**
 * @brief   Put back any volatile configuration loaderFlashReadPage() changed,
 *          as found. Called by Serve() before it returns.
 * @return  false if the restore could not be confirmed.
 */
bool loaderFlashRestore(void);
/** @} */
#endif

#if LOADER_ALLOW_WRITE
/**
 * @brief   Erase one sector and prove it blank.
 *
 * @details Skips the erase when the sector already reads blank. Otherwise
 *          clears block protection if set, erases, waits within the part's
 *          measured budget, and reads the sector back. A protected array
 *          silently ignores Erase on some parts, so success is defined by the
 *          read-back, not by the busy bit.
 *
 * @param[in] offset    Any offset within the sector.
 * @return  true only when the whole sector reads as erased afterwards.
 */
bool loaderFlashEraseSector(uint32_t offset);

/**
 * @brief   Program bytes and prove they read back.
 *
 * @details Splits the range at page boundaries, clears block protection if
 *          set, programs each page within the part's measured budget, then
 *          reads the whole range back and compares. NOR programming can only
 *          clear bits, so programming over data that is not erased fails the
 *          comparison rather than leaving a silent mix of old and new.
 *
 * @param[in] offset    Flash byte offset of the first byte.
 * @param[in] buf       Bytes to program.
 * @param[in] n         Byte count; offset + n must not exceed the array.
 * @return  true only when every byte reads back as written.
 */
bool loaderFlashProgram(uint32_t offset, const uint8_t *buf, uint32_t n);
#endif

#endif /* LOADER_FLASH_H */
