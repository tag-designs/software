/**
 * @file storage_gd5f.h
 * @brief GD5F SPI-NAND geometry and storage operation table.
 * @author tag firmware authors
 * @date 2026-07-17
 */

#ifndef TAG_STORAGE_GD5F_H
#define TAG_STORAGE_GD5F_H

#include "storage_device.h"

/*
 * Defaults match the current 1 Gbit GD5F NAND target. A board or module can
 * override these with compile-time definitions for another density.
 */
#ifndef GD5F_PAGE_SIZE
#define GD5F_PAGE_SIZE                 2048UL
#endif
#ifndef GD5F_SPARE_SIZE
#define GD5F_SPARE_SIZE                64UL
#endif
#ifndef GD5F_PAGES_PER_BLOCK
#define GD5F_PAGES_PER_BLOCK           64UL
#endif
#ifndef GD5F_PHYSICAL_BLOCK_COUNT
#define GD5F_PHYSICAL_BLOCK_COUNT      1024UL
#endif
#ifndef GD5F_MIN_VALID_BLOCK_COUNT
#define GD5F_MIN_VALID_BLOCK_COUNT     1004UL
#endif
#ifndef GD5F_LOGICAL_BLOCK_COUNT
#define GD5F_LOGICAL_BLOCK_COUNT       GD5F_MIN_VALID_BLOCK_COUNT
#endif

#define GD5F_BLOCK_SIZE                (GD5F_PAGE_SIZE * GD5F_PAGES_PER_BLOCK)
#define GD5F_LOGICAL_PAGE_COUNT \
  (GD5F_LOGICAL_BLOCK_COUNT * GD5F_PAGES_PER_BLOCK)
#define GD5F_MAP_ERASED_ENTRY          0xffffU

/**
 * @enum gd5f_page_read_result_t
 * @brief Result classes returned by explicit NAND page reads.
 *
 * @details GD5F status reports whether on-die ECC corrected or could not
 *          correct the page after a PAGE_READ operation. Download and recovery
 *          paths use this to skip unreadable pages without treating every
 *          missing page as a transport failure.
 */
typedef enum {
  GD5F_PAGE_READ_OK,               ///< Page read completed without ECC notice.
  GD5F_PAGE_READ_ECC_CORRECTED,    ///< Page read completed with corrected ECC.
  GD5F_PAGE_READ_ECC_UNCORRECTABLE,///< Page has uncorrectable ECC.
  GD5F_PAGE_READ_ERROR             ///< Command, mapping, or bus read failed.
} gd5f_page_read_result_t;

/**
 * @brief CPU-flash-resident logical-to-physical block map.
 *
 * @details The first GD5F_LOGICAL_BLOCK_COUNT entries map logical blocks to
 *          physical NAND blocks. Remaining entries are reserved and normally
 *          remain erased. Entry 0 equal to 0xffff means the map has not been
 *          provisioned.
 */
extern uint16_t gd5fLogicalBlockMap[GD5F_PHYSICAL_BLOCK_COUNT];

/**
 * @brief Return true when the flat NAND map is present rather than erased.
 *
 * @return true when the first map entry is programmed, false when the map page
 *         still appears erased.
 */
bool gd5fLogicalMapConfigured(void);

/**
 * @brief Validate the flat NAND map stored in internal flash.
 *
 * @return true when every logical entry is in range and strictly increasing.
 */
bool gd5fLogicalMapValidate(void);

/**
 * @brief Convert a logical NAND page to the physical page selected by the map.
 *
 * @param[in] logical_page Logical page index.
 * @param[out] physical_page Physical page index on success.
 * @return true when the map is present, valid, and the page is in range.
 *
 * @warning Requires the CPU-flash logical map to have been provisioned.
 */
bool gd5fMapLogicalPage(uint32_t logical_page, uint32_t *physical_page);

/**
 * @brief Read a complete physical NAND page and report ECC/read status.
 *
 * @param[in] dev Storage device descriptor for the NAND.
 * @param[in] physical_page Physical page index.
 * @param[out] buf Destination buffer, normally GD5F_PAGE_SIZE bytes.
 * @param[in] len Number of bytes to read from column zero.
 * @return Read status including uncorrectable ECC.
 */
gd5f_page_read_result_t gd5fReadPhysicalPage(
    const TagStorageDevice *dev, uint32_t physical_page, uint8_t *buf,
    uint32_t len);

/**
 * @brief Read a complete logical NAND page and report ECC/read status.
 *
 * @param[in] dev Storage device descriptor for the NAND.
 * @param[in] logical_page Logical page index.
 * @param[out] buf Destination buffer, normally GD5F_PAGE_SIZE bytes.
 * @param[in] len Number of bytes to read from column zero.
 * @return Read status including uncorrectable ECC or mapping failure.
 */
gd5f_page_read_result_t gd5fReadLogicalPage(
    const TagStorageDevice *dev, uint32_t logical_page, uint8_t *buf,
    uint32_t len);

/**
 * @brief Scan and log factory bad-block markers without modifying flash.
 *
 * @details Reads the first two marker pages of every physical block and writes
 *          any non-erased or unreadable marker locations to the debug log. This
 *          is safe to run before provisioning because it does not erase or
 *          program STM32 or NAND flash.
 *
 * @param[in] dev Storage device descriptor for the NAND.
 * @param[out] bad_block_count Optional count of physical blocks whose marker
 * was not erased.
 * @return true when all marker reads completed.
 */
bool gd5fLogFactoryBadBlocks(const TagStorageDevice *dev,
                             uint32_t *bad_block_count);

/**
 * @brief Scan factory bad-block markers and write the flat NAND map.
 *
 * @details Builds a logical-to-physical map from blocks whose factory bad-block
 *          marker bytes are erased, then programs that map into the reserved
 *          STM32 internal-flash map region.
 *
 * @param[in] dev Storage device descriptor for the NAND.
 * @param[out] bad_block_count Optional count of physical blocks rejected while
 * scanning.
 * @return true when enough good blocks were found and the map was programmed;
 *         false on probe failure, unreadable markers, too few good blocks, or
 *         STM32 flash programming failure.
 */
bool gd5fProvisionLogicalMap(const TagStorageDevice *dev,
                             uint32_t *bad_block_count);

/** Chip-specific operation table for the configured GD5F SPI-NAND geometry. */
extern const TagStorageOps gd5fStorageOps;

#endif
