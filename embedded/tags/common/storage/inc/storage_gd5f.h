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
 * CPU-flash-resident logical-to-physical block map.
 *
 * The first GD5F_LOGICAL_BLOCK_COUNT entries map logical blocks to
 * physical NAND blocks. Remaining entries are reserved and normally remain
 * erased. Entry 0 equal to 0xffff means the map has not been provisioned.
 */
extern uint16_t gd5fLogicalBlockMap[GD5F_PHYSICAL_BLOCK_COUNT];

/**
 * Return true when the flat NAND map is present rather than erased.
 */
bool gd5fLogicalMapConfigured(void);

/**
 * Validate the flat NAND map stored in internal flash.
 */
bool gd5fLogicalMapValidate(void);

/**
 * Scan factory bad-block markers and write the flat NAND map to internal flash.
 *
 * @param[in] dev Storage device descriptor for the NAND.
 * @param[out] bad_block_count Optional count of physical blocks rejected while
 * scanning.
 * @return true when enough good blocks were found and the map was programmed.
 */
bool gd5fProvisionLogicalMap(const TagStorageDevice *dev,
                             uint32_t *bad_block_count);

/** Chip-specific operation table for the configured GD5F SPI-NAND geometry. */
extern const TagStorageOps gd5fStorageOps;

#endif
