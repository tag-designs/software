/**
 * @file    dev_inf.c
 * @brief   STM32CubeProgrammer descriptor for the IMUTagNandv2 GD5F loader.
 *
 * @details Describes the data area only (2048-byte pages, 64 to a
 *          128 KiB block, 2048 blocks), which is what the ST entry points'
 *          linear Read serves. Spare bytes are read through
 *          Serve(). The device name carries the variant and commit.
 */

#include "dev_inf.h"

#include "loader.h"
#include "version.h"

/** @brief Data bytes per block. */
#define GD5F_LOADER_BLOCK_BYTES (GD5F_PAGE_SIZE * GD5F_PAGES_PER_BLOCK)

LOADER_DEV_INFO const struct StorageInfo StorageInfo = {
    "GD5F2GM7RE_IMUTagNandv2 RO " VERSION_HASH,
    NAND_FLASH,
    LOADER_DEVICE_BASE,
    GD5F_LOADER_BLOCK_BYTES * GD5F_PHYSICAL_BLOCK_COUNT,
    GD5F_PAGE_SIZE,
    0xFFU,
    {
        {GD5F_PHYSICAL_BLOCK_COUNT, GD5F_LOADER_BLOCK_BYTES},
        {0U, 0U},
    },
};
