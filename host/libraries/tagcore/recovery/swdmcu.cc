/**
 * @file    swdmcu.cc
 * @brief   MCU maps for SWD capture; see swdmcu.h for sources and status.
 */

#include "recovery/swdmcu.h"

namespace tagcore::recovery {

namespace {

// STM32L432 (RM0394 Rev 5, stm32l432xx.h). Used on hardware 2026-09-30.
const McuMap kStm32L432 = {
    "STM32L432",
    0x435,
    0xE0042000, // DBGMCU_IDCODE
    0xE0042008, // DBGMCU_APB1FZR1
    1u << 12,   // DBG_IWDG_STOP

    0x08000000, // flash
    2048,       // 2 KB pages
    0x1FFF75E0, // flash size (KB)
    0x1FFF7590, // UID

    0x40022020, // FLASH_OPTR
    0x40021094, // RCC_CSR
    0x40021090, // RCC_BDCR
    0x40021058, // RCC_APB1ENR1
    1u << 10,   // RTCAPBEN
    1u << 28,   // PWREN
    0x40002800, // RTC_TR
    0x40002804, // RTC_DR
    0x40002850, // RTC_BKP0R
    32,

    {0x40022018}, // FLASH_ECCR

    {
        {"rcc", 0x40021000, 0xA0},
        {"flash_regs", 0x40022000, 0x50},
        {"pwr", 0x40007000, 0x68},
        {"rtc", 0x40002800, 0xD0}, // includes the backup registers
        {"dbgmcu", 0xE0042000, 0x10},
    },

    {
        {"otp", 0x1FFF7000, 0x400},
        {"option_bytes", 0x1FFF7800, 0x10},
    },

    {
        {"sram1", 0x20000000, 48 * 1024},
        {"sram2", 0x10000000, 16 * 1024},
    },

    0x1A0, // identity record: after 104 vectors
};

// STM32U375 (RM0487 Rev 3, stm32u375xx.h). Checked against an
// IMUTagNandBmp581 on 2026-10-02.
// Nonsecure aliases; tags run with TrustZone off.
const McuMap kStm32U375 = {
    "STM32U375",
    0x454,
    0xE0044000, // DBGMCU_IDCODE
    0xE0044008, // DBGMCU_APB1FZR1
    1u << 12,   // DBG_IWDG_STOP

    0x08000000, // flash
    4096,       // 4 KB pages
    0x0BFA07A0, // flash size (KB)
    0x0BFA0700, // UID

    0x40022040, // FLASH_OPTR
    0x40030D14, // RCC_CSR
    0x40030D10, // RCC_BDCR
    0x40030C9C, // RCC_APB1ENR1
    1u << 30,   // RTCAPBEN
    0,          // PWR is on AHB here; no PWREN needed
    0x40007800, // RTC_TR
    0x40007804, // RTC_DR
    0x40007D00, // TAMP_BKP0R
    32,

    {0x40022030, 0x40022034}, // FLASH_ECCCR, FLASH_ECCDR

    {
        {"rcc", 0x40030C00, 0x118},
        {"flash_regs", 0x40022000, 0x44},
        {"rtc", 0x40007800, 0x80},
        {"tamp", 0x40007C00, 0x180}, // includes the backup registers
        {"dbgmcu", 0xE0044000, 0x10},
    },

    {
        {"otp", 0x0BFA0000, 0x200},
        // Option bytes are not memory-mapped on the U3; FLASH_OPTR only.
    },

    {
        {"sram1", 0x20000000, 192 * 1024},
        {"sram2", 0x20030000, 64 * 1024},
    },

    0x240, // identity record: after 144 vectors
};

} // namespace

const std::vector<const McuMap *> &AllMcuMaps() {
  static const std::vector<const McuMap *> maps = {&kStm32L432, &kStm32U375};
  return maps;
}

const McuMap *FindMcuMap(uint32_t idcode) {
  for (const McuMap *m : AllMcuMaps())
    if ((idcode & 0xFFFu) == m->dev_id)
      return m;
  return nullptr;
}

} // namespace tagcore::recovery
