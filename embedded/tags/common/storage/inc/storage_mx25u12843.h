/**
 * @file storage_mx25u12843.h
 * @brief MX25U12843 external flash geometry and storage operation table.
 * @author tag firmware authors
 * @date 2026-07-07
 */

#ifndef TAG_STORAGE_MX25U12843_H
#define TAG_STORAGE_MX25U12843_H

#include "storage_device.h"

#define MX25U12843_SIZE                     (1024UL * 1024UL * 16UL)
#define MX25U12843_SECTOR_SIZE              (4096UL)
#define MX25U12843_SECTOR_COUNT             (MX25U12843_SIZE / MX25U12843_SECTOR_SIZE)

/** Chip-specific operation table for MX25U12843-compatible external flash. */
extern const TagStorageOps mx25u12843StorageOps;

#endif
