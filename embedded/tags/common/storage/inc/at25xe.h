/**
 * @file at25xe.h
 * @brief AT25XE external flash geometry and storage operation table.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef _AT25XE_H_
#define _AT25XE_H_

#include "storage_device.h"

#define AT25XE_SIZE                            (1024UL * 1024UL * 4UL)
#define AT25XE_SECTOR_SIZE                     (4096UL)
#define AT25XE_SECTOR_COUNT                    (AT25XE_SIZE / AT25XE_SECTOR_SIZE)

/** Chip-specific operation table for AT25XE-compatible external flash. */
extern const TagStorageOps at25xeStorageOps;
  
#endif 
