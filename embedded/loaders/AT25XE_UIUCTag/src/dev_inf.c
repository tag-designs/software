/**
 * @file    dev_inf.c
 * @brief   STM32CubeProgrammer descriptor for the UIUCTag AT25XE loader.
 *
 * @details The device name carries the image variant and the commit it was
 *          built from, so STM32CubeProgrammer shows which loader is in use:
 *          a mismatched loader reads plausible garbage without complaint.
 *          "RO" is the forensic image; "RW" erases and programs, for rescue
 *          and bench testing.
 *
 *          The sector map is the 4 kB erase unit the loader actually uses, so
 *          the programmer's idea of a sector matches what an erase touches.
 */

#include "dev_inf.h"

#include "at25xe_commands.h"
#include "loader.h"
#include "version.h"

/** @brief Image variant shown in the device name: "RO" or "RW". */
#if LOADER_ALLOW_WRITE
#define LOADER_VARIANT "RW"
#else
#define LOADER_VARIANT "RO"
#endif

/** @brief Descriptor read by STM32CubeProgrammer; see dev_inf.h. */
LOADER_DEV_INFO const struct StorageInfo StorageInfo = {
    "AT25XE_UIUCTag " LOADER_VARIANT " " VERSION_HASH,
    NOR_FLASH,
    LOADER_DEVICE_BASE,
    AT25XE_SIZE,
    256U,
    0xFFU,
    {
        {AT25XE_SECTOR_COUNT, AT25XE_SECTOR_SIZE},
        {0U, 0U},
    },
};
