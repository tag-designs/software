/**
 * @file    gd5f_commands.h
 * @brief   GigaDevice GD5F SPI-NAND command set, feature registers and
 *          timing, shared by the firmware driver (gd5f.c) and the SRAM
 *          loaders (embedded/loaders).
 *
 * @details Dependency-free, like at25xe_commands.h, so that a loader built
 *          without the tag runtime uses exactly the firmware's opcodes. The
 *          geometry (page, spare, block counts) stays with the per-part
 *          module defines (common/modules/flash_gd5f*.mk) and storage_gd5f.h.
 *
 *          Figures are from the GD5F2GM7RE datasheet, DS_00819 Rev 1.3.
 */

#ifndef GD5F_COMMANDS_H
#define GD5F_COMMANDS_H

/** @name Commands
 * @{
 */
#define GD5F_CMD_WRITE_ENABLE        0x06U ///< Set WEL; precedes program and erase.
#define GD5F_CMD_GET_FEATURE         0x0FU ///< Read a feature register (address byte follows).
#define GD5F_CMD_SET_FEATURE         0x1FU ///< Write a feature register (address, data follow).
#define GD5F_CMD_PAGE_READ           0x13U ///< Array page -> cache; 24-bit row address follows.
#define GD5F_CMD_READ_CACHE          0x03U ///< Cache -> host; 16-bit column and one dummy byte.
#define GD5F_CMD_PROGRAM_LOAD        0x02U ///< Host -> cache, clearing the rest of the cache.
#define GD5F_CMD_PROGRAM_LOAD_RANDOM 0x84U ///< Host -> cache, keeping the rest.
#define GD5F_CMD_PROGRAM_EXECUTE     0x10U ///< Cache -> array page.
#define GD5F_CMD_BLOCK_ERASE         0xD8U ///< Erase a 64-page block.
#define GD5F_CMD_READ_ID             0x9FU ///< One dummy byte, then manufacturer and device ID.
#define GD5F_CMD_DEEP_POWER_DOWN     0xB9U ///< Enter deep power-down.
#define GD5F_CMD_RELEASE_DPD         0xABU ///< Leave deep power-down.
#define GD5F_CMD_RESET               0xFFU ///< Reset; clears the status bits, keeps A0/B0.
/** @} */

/** @name Feature registers and bits
 * @{
 */
#define GD5F_FEATURE_BLOCK_LOCK      0xA0U ///< Block protection; BP2-0 = 111 after power-on.
#define GD5F_FEATURE_CONFIG          0xB0U ///< Configuration; holds ECC_EN.
#define GD5F_FEATURE_STATUS          0xC0U ///< Status of the last operation.
#define GD5F_FEATURE_STATUS2         0xF0U ///< Extended ECC status (ECCSE).

#define GD5F_CONFIG_OTP_PRT          0x80U ///< B0: OTP protect. NON-VOLATILE: setting it is permanent.
#define GD5F_CONFIG_OTP_EN           0x40U ///< B0: page reads and programs address the OTP area.
#define GD5F_CONFIG_ECC_EN           0x10U ///< B0: on-die ECC enabled (power-on default).
#define GD5F_CONFIG_BPL              0x08U ///< B0: block-protection lock-down.
#define GD5F_CONFIG_QE               0x01U ///< B0: quad enable; off makes IO2/IO3 WP#/HOLD#.
#define GD5F_CONFIG_RESERVED         0x26U ///< B0: reserved bits, held low on any write.

#define GD5F_STATUS_OIP              0x01U ///< C0: operation in progress.
#define GD5F_STATUS_WEL              0x02U ///< C0: write enable latch.
#define GD5F_STATUS_E_FAIL           0x04U ///< C0: last erase failed.
#define GD5F_STATUS_P_FAIL           0x08U ///< C0: last program failed.
#define GD5F_STATUS_ECC_MASK         0x30U ///< C0: ECCS, verdict of the last page read.
#define GD5F_STATUS_ECC_CORRECTED    0x10U ///< ECCS = 01: errors detected and corrected.
#define GD5F_STATUS_ECC_UNCORRECTABLE 0x20U ///< ECCS = 10: uncorrectable.
/** @} */

/** @name Timing, from the datasheet
 * @{
 */
#define GD5F_TRD_ECC_MAX_US          120U ///< Page read to cache with ECC, maximum.
#define GD5F_TRST_MAX_US             500U ///< Reset, maximum.
#define GD5F_TVSL_MS                 2U   ///< VCC(min) to first command after power-up.
#define GD5F_DPD_ENTRY_DELAY_US      3U   ///< tDP: deep power-down entry.
#define GD5F_DPD_RELEASE_DELAY_US    30U  ///< tRES1: release from deep power-down.
/** @} */

#endif /* GD5F_COMMANDS_H */
