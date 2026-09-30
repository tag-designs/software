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
