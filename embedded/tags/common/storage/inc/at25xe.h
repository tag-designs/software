/**
 * @file at25xe.h
 * @brief AT25XE external flash storage operation table.
 * @author tag firmware authors
 * @date 2026-05-23
 *
 * @details Geometry, opcodes, register bits and timing budgets live in
 *          at25xe_commands.h so the external loader can share them.
 */

#ifndef _AT25XE_H_
#define _AT25XE_H_

#include "at25xe_commands.h"
#include "storage_device.h"

/** Chip-specific operation table for AT25XE-compatible external flash. */
extern const TagStorageOps at25xeStorageOps;

#endif
