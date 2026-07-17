/**
 * @file storage_gd5f1gq5re.h
 * @brief GD5F1GQ5RE SPI-NAND geometry and storage operation table.
 * @author tag firmware authors
 * @date 2026-07-17
 */

#ifndef TAG_STORAGE_GD5F1GQ5RE_H
#define TAG_STORAGE_GD5F1GQ5RE_H

#include "storage_device.h"

#define GD5F1GQ5RE_PAGE_SIZE                 2048UL
#define GD5F1GQ5RE_SPARE_SIZE                64UL
#define GD5F1GQ5RE_PAGES_PER_BLOCK           64UL
#define GD5F1GQ5RE_BLOCK_SIZE                (GD5F1GQ5RE_PAGE_SIZE * GD5F1GQ5RE_PAGES_PER_BLOCK)
#define GD5F1GQ5RE_PHYSICAL_BLOCK_COUNT      1024UL
#define GD5F1GQ5RE_MIN_VALID_BLOCK_COUNT     1004UL

#define GD5F1GQ5RE_LOGICAL_BLOCK_COUNT       GD5F1GQ5RE_MIN_VALID_BLOCK_COUNT
#define GD5F1GQ5RE_MAX_BAD_BLOCKS            32UL
#define GD5F1GQ5RE_BBT_MAGIC                 0x54424247UL
#define GD5F1GQ5RE_BBT_VERSION               1UL

/** CPU-flash-resident bad-block list consumed by the GD5F1GQ5RE driver. */
typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t physical_block_count;
  uint32_t logical_block_count;
  uint32_t bad_block_count;
  uint32_t checksum;
  uint32_t reserved[2];
  uint16_t bad_blocks[GD5F1GQ5RE_MAX_BAD_BLOCKS];
} TagGd5f1gq5reBadBlockTable;

/**
 * Optional CPU-flash bad-block table.
 *
 * A target can provide this symbol in a linker-controlled STM32L432 flash
 * erase page that is not erased by the normal tag data erase path. The record
 * is small, but the reservation/protection unit should be a whole 2 KiB MCU
 * flash page. If absent or invalid, the driver scans factory bad-block markers
 * into RAM for bring-up; it does not write the table back to NAND.
 */
extern const TagGd5f1gq5reBadBlockTable gd5f1gq5reBadBlockTable
    __attribute__((weak));

/** Chip-specific operation table for GD5F1GQ5RE-compatible SPI-NAND. */
extern const TagStorageOps gd5f1gq5reStorageOps;

#endif
