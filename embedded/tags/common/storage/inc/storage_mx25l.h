/**
 * @file storage_mx25l.h
 * @brief MX25L external flash geometry and storage operation table.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_STORAGE_MX25L_H
#define TAG_STORAGE_MX25L_H

#include "storage_device.h"

#define MX25L_SECTOR_SIZE                     (4096)

/** Chip-specific operation table for MX25L-compatible external flash. */
extern const TagStorageOps mx25lStorageOps;

#endif
