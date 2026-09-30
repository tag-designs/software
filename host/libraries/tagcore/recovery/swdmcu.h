/**
 * @file    swdmcu.h
 * @brief   Per-MCU memory and register maps used by SWD capture.
 *
 * @details Everything the capture needs to know about a part lives here, in
 *          one table per MCU, so the procedures in statecapture.cc stay
 *          MCU-neutral. Addresses come from RM0394 Rev 5 (STM32L43x) and
 *          RM0487 Rev 3 (STM32U3), cross-checked against the CMSIS headers in
 *          the ChibiOS submodule. See the MCU reference section of
 *          host/libraries/tagcore/design/swd-recovery.md.
 *
 * @note    The STM32L432 table has been used on hardware. The STM32U375 table
 *          is from the manual and headers only and has not been run against a
 *          tag; treat its register-block sizes in particular as unverified.
 */

#ifndef TAGCORE_RECOVERY_SWDMCU_H
#define TAGCORE_RECOVERY_SWDMCU_H

#include <cstdint>
#include <vector>

namespace tagcore::recovery {

/**
 * @struct  McuRegion
 * @brief   A named, contiguous address range captured as one file.
 */
struct McuRegion {
  const char *name; ///< Short name, used as the capture file stem.
  uint32_t addr;    ///< First byte.
  uint32_t size;    ///< Length in bytes; a multiple of 4.
};

/**
 * @struct  McuMap
 * @brief   Addresses and register positions for one MCU.
 *
 * @details A zero address means "not present on this part".
 */
struct McuMap {
  const char *name;         ///< Part name, e.g. "STM32L432".
  uint16_t dev_id;          ///< DBGMCU_IDCODE DEV_ID[11:0].
  uint32_t dbgmcu_idcode;   ///< DBGMCU_IDCODE address.
  uint32_t dbgmcu_apb1fzr1; ///< DBGMCU_APB1FZR1 address.
  uint32_t iwdg_freeze;     ///< DBG_IWDG_STOP mask in DBGMCU_APB1FZR1.

  uint32_t flash_base;      ///< Main flash start.
  uint32_t flash_page;      ///< Erase page size; the capture read unit.
  uint32_t flashsize_reg;   ///< Flash size data register (16-bit, KB).
  uint32_t uid;             ///< 96-bit unique device ID.

  uint32_t flash_optr;      ///< FLASH_OPTR (live option bytes).
  uint32_t rcc_csr;         ///< RCC_CSR (reset flags).
  uint32_t rcc_bdcr;        ///< RCC_BDCR (backup domain control).
  uint32_t rcc_apb1enr1;    ///< RCC_APB1ENR1, holding RTCAPBEN (and PWREN on L4).
  uint32_t rtcapben;        ///< RTCAPBEN mask: enables backup-register reads.
  uint32_t pwren;           ///< PWREN mask in rcc_apb1enr1, or 0.
  uint32_t rtc_tr;          ///< RTC_TR (time).
  uint32_t rtc_dr;          ///< RTC_DR (date).
  uint32_t backup_regs;     ///< First backup register.
  uint32_t backup_count;    ///< Number of 32-bit backup registers.

  /// ECC registers, read before and after the flash (evidence of the tag's
  /// own last ECC fault; a flash read can overwrite them).
  std::vector<uint32_t> ecc_regs;

  /// Raw register blocks saved verbatim, for evidence beyond the named values.
  std::vector<McuRegion> register_blocks;

  /// Information-block regions (OTP, memory-mapped option bytes).
  std::vector<McuRegion> info_regions;

  /// SRAM regions, in capture order.
  std::vector<McuRegion> sram;
};

/**
 * @brief   Find the map for a DBGMCU_IDCODE value.
 *
 * @param[in] idcode  Raw DBGMCU_IDCODE.
 * @return  The map, or nullptr for an unsupported part.
 */
const McuMap *FindMcuMap(uint32_t idcode);

/**
 * @brief   All supported maps, for probing the IDCODE address of each.
 */
const std::vector<const McuMap *> &AllMcuMaps();

} // namespace tagcore::recovery

#endif
