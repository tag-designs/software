/**
 * @file at25xe_commands.h
 * @brief AT25XE geometry, command set, register bits and timing budgets.
 *
 * @details Dependency-free on purpose: it is shared by the firmware driver in
 *          at25xe.c and by the STM32CubeProgrammer external loader in
 *          embedded/loaders/, which runs without the ChibiOS kernel and cannot
 *          include the tag storage headers. The budgets encode measured device
 *          behaviour; keeping one copy means a budget raised for the firmware
 *          is raised for the loader too.
 *
 *          Reference: Renesas DS-AT25XE321D-160 Rev. P (Dec 2024). Datasheet
 *          maxima, for comparison with the measured budgets below: 4 kB
 *          sector erase 150 ms, 64 kB block erase 2250 ms, chip erase 65-75 s
 *          typical with no maximum given, Write Status Register 37 ms, resume
 *          from Ultra-Deep Power-Down 200 us (up to 1200 us after a short
 *          UDPD hold), Read Array (03h) up to 40 MHz.
 */

#ifndef AT25XE_COMMANDS_H
#define AT25XE_COMMANDS_H

#include <stdint.h>

/** @name AT25XE geometry
 * @{
 */
#define AT25XE_SIZE                            (1024UL * 1024UL * 4UL) ///< Array size in bytes (AT25XE321D, 32 Mbit).
#define AT25XE_SECTOR_SIZE                     (4096UL)                ///< Smallest erase unit (20h), in bytes.
#define AT25XE_SECTOR_COUNT                    (AT25XE_SIZE / AT25XE_SECTOR_SIZE) ///< Number of 4 kB sectors.
/** @} */

/**
 * @brief Delay after Resume from (Ultra-)Deep Power-Down (ABh), in ms.
 *
 * @details Covers the datasheet's worst-case tRUDPD of 1200 us after a short
 *          Ultra-Deep Power-Down hold, with margin.
 */
#define AT25XE_WAKE_DELAY_MS 2

/** @name AT25XE timing budgets
 * Poll intervals and iteration limits for operations that assert WIP.
 * @{
 */
/** @brief Delay between page-program commands, in milliseconds (unused). */
#define AT25XE_INTER_WRITE_DELAY 2
/** @brief WIP poll interval while a page program completes, in microseconds. */
#define AT25XE_PAGE_PROG_POLL_INTERVAL_US 100
/** @brief Maximum page-program WIP polls before reporting failure. */
#define AT25XE_PAGE_PROG_POLL_LIMIT 120
/*
 * Write Status Register's own write cycle (tW) was measured taking up to
 * ~11.4 ms (114 iterations at AT25XE_PAGE_PROG_POLL_INTERVAL_US), right at the
 * edge of the page-program budget above -- occasionally over it, at which
 * point at25xeUnprotect() timed out and returned with WIP still genuinely set
 * on the part. Give it its own, more generous budget.
 */
/** @brief WIP poll interval after Write Status Register, in microseconds. */
#define AT25XE_WRSR_POLL_INTERVAL_US 200
/** @brief Maximum Write Status Register WIP polls (50 ms total). */
#define AT25XE_WRSR_POLL_LIMIT 250
/** @brief WIP poll interval while a 4 KB sector erase completes, in milliseconds. */
#define AT25XE_SECTOR_ERASE_POLL_INTERVAL 150
/*
 * The original 5-iteration budget (750 ms) left a residual ~4% failure rate
 * under repeated testing -- occasional sector erases genuinely take longer.
 * Wake and erase are rare events (once per checkpoint), so a larger budget
 * costs nothing in the common case and avoids silently dropping a write.
 */
/** @brief Maximum sector-erase WIP polls (3 s total). */
#define AT25XE_SECTOR_ERASE_POLL_LIMIT 20
/** @} */

/** @name AT25XE command opcodes
 * @{
 */
#define AT25XE_CMD_READ            0x03 ///< Read Array, 24-bit address, no dummy byte; up to 40 MHz.
#define AT25XE_CMD_PAGE_PROG       0x02 ///< Page Program; must not cross a 256-byte page.
#define AT25XE_CMD_SECTOR_ERASE    0x20 ///< Erase the 4 kB sector containing the address.
#define AT25XE_CMD_BLOCK_ERASE_64K 0xD8 ///< Erase the 64 kB block containing the address (unused).
#define AT25XE_CMD_READ_ID         0x9F ///< JEDEC ID: manufacturer, device ID 1, device ID 2.
#define AT25XE_CMD_WRITE_ENABLE    0x06 ///< Set WEL; required before program, erase and status writes.
#define AT25XE_CMD_READ_STATUS_REG 0x05 ///< Read Status Register 1.
#define AT25XE_CMD_WRITE_STATUS_REG_1 0x01 ///< Write Status Register 1; asserts BSY for tWRSR.
#define AT25XE_CMD_DEEP_POWER_DOWN 0xB9 ///< Deep Power-Down (or UDPD, depending on SR4 PDM).
#define AT25XE_CMD_ULTRA_DEEP_POWER_DOWN 0x79 ///< Ultra-Deep Power-Down; exit only with ABh or reset.
#define AT25XE_CMD_POWER_UP        0xAB ///< Resume from DPD/UDPD; from UDPD performs an internal reset.
#define AT25XE_CMD_RESET_ENABLE    0x66 ///< First half of the software reset sequence (unused).
#define AT25XE_CMD_RESET_MEMORY    0x99 ///< Second half of the software reset sequence (unused).
/** @} */

/** @name AT25XE JEDEC identity
 * @{
 */
/** @brief JEDEC manufacturer ID byte (Adesto/Renesas). */
#define AT25XE_JEDEC_MANUFACTURER  0x1F
/** @brief JEDEC device ID byte 1 accepted by the drivers. */
#define AT25XE_JEDEC_DEVICE1       0x47
/** @} */

/** @name AT25XE register bits
 * @{
 */
/*
 * Status Register 1. On the AT25XE321D the bits are SRP0 (7), BPSIZE (6),
 * TB (5), BP2:0 (4:2), WEL (1) and RDY/BSY (0). The QE and SRWD names below
 * are inherited from another part and do not describe this one: 0x40 is
 * BPSIZE and 0x80 is SRP0. AT25XE_FLAGS_SR_BP (0x3C) covers TB and BP2:0.
 * Writing 0x00 clears all four protection fields under the factory-default
 * standard protection scheme (SR3 WPS = 0).
 */

#define AT25XE_FLAGS_SR_WIP                    ((uint8_t)0x01)    ///< RDY/BSY: an internal operation is in progress.
#define AT25XE_FLAGS_SR_WEL                    ((uint8_t)0x02)    ///< Write enable latch.
#define AT25XE_FLAGS_SR_BP                     ((uint8_t)0x3C)    ///< Block protect: TB and BP2:0.
#define AT25XE_FLAGS_SR_QE                     ((uint8_t)0x40)    ///< Named QE; on the AT25XE321D this is BPSIZE.
#define AT25XE_FLAGS_SR_SRWD                   ((uint8_t)0x80)    ///< Named SRWD; on the AT25XE321D this is SRP0.

/*
 * The configuration and security register bits below come from the same
 * earlier part as the QE and SRWD names and have not been checked against the
 * AT25XE321D datasheet. Neither driver uses them.
 */

#define AT25XE_FLAGS_CR1_TB                    ((uint8_t)0x08)    ///< Configuration Register 1: top/bottom (unverified).

#define AT25XE_FLAGS_CR2_LH_SWITCH             ((uint8_t)0x02)    ///< Configuration Register 2: low-power/high-performance (unverified).

#define AT25XE_FLAGS_SECR_SOI                  ((uint8_t)0x01)    ///< Security Register: secured OTP indicator (unverified).
#define AT25XE_FLAGS_SECR_LDSO                 ((uint8_t)0x02)    ///< Security Register: lock-down secured OTP (unverified).
#define AT25XE_FLAGS_SECR_PSB                  ((uint8_t)0x04)    ///< Security Register: program suspended (unverified).
#define AT25XE_FLAGS_SECR_ESB                  ((uint8_t)0x08)    ///< Security Register: erase suspended (unverified).
#define AT25XE_FLAGS_SECR_P_FAIL               ((uint8_t)0x20)    ///< Security Register: program failed (unverified).
#define AT25XE_FLAGS_SECR_E_FAIL               ((uint8_t)0x40)    ///< Security Register: erase failed (unverified).
/** @} */

#endif
