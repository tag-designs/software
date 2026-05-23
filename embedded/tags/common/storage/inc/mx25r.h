/**
 * @file mx25r.h
 * @brief MX25R external flash geometry and storage operation table.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef _MX25R_H_
#define _MX25R_H_

#include "storage_device.h"

#define MX25R_SIZE                            (1024UL * 1024UL * 4UL)
#define MX25R_SECTOR_SIZE                     (4096UL)
#define MX25R_SECTOR_COUNT                    (MX25R_SIZE / MX25R_SECTOR_SIZE)

/** Chip-specific operation table for MX25R-compatible external flash. */
extern const TagStorageOps mx25rStorageOps;

#endif 
