//! Per-MCU memory and register maps used by SWD capture.
//!
//! Everything the capture needs to know about a part lives here, in one
//! table per MCU, so the procedures in [`super::capture`] stay MCU-neutral.
//! Port of `tagcore/recovery/swdmcu.cc`; the values are the same, from the
//! STM32L43x and STM32U3 reference manuals, cross-checked against the CMSIS
//! headers in the ChibiOS submodule. See the MCU reference section of
//! `host/libraries/tagcore/design/swd-recovery.md`.
//!
//! The STM32L432 table has been used on hardware since 2026-09-30, the
//! STM32U375 table since 2026-10-02 (an IMUTagNandBmp581).

/// A named, contiguous address range captured as one file.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct McuRegion {
    /// Short name, used as the capture file stem (`"rcc"` becomes `rcc.bin`).
    pub name: &'static str,
    /// First byte.
    pub addr: u32,
    /// Length in bytes; a multiple of 4.
    pub size: u32,
}

/// Addresses and register positions for one MCU.
///
/// A zero address or mask means "not present on this part".
#[derive(Debug, PartialEq, Eq)]
pub struct McuMap {
    /// Part name, e.g. `"STM32L432"`.
    pub name: &'static str,
    /// `DBGMCU_IDCODE` `DEV_ID[11:0]`.
    pub dev_id: u16,
    /// `DBGMCU_IDCODE` address.
    pub dbgmcu_idcode: u32,
    /// `DBGMCU_APB1FZR1` address.
    pub dbgmcu_apb1fzr1: u32,
    /// `DBG_IWDG_STOP` mask in `DBGMCU_APB1FZR1`.
    pub iwdg_freeze: u32,
    /// Main flash start.
    pub flash_base: u32,
    /// Erase page size in bytes; the capture's read unit for internal flash.
    pub flash_page: u32,
    /// Flash size data register (16-bit, in KB).
    pub flashsize_reg: u32,
    /// 96-bit unique device ID, three words.
    pub uid: u32,
    /// `FLASH_OPTR` (the live option bytes).
    pub flash_optr: u32,
    /// `RCC_CSR` (reset flags).
    pub rcc_csr: u32,
    /// `RCC_BDCR` (backup domain control).
    pub rcc_bdcr: u32,
    /// `RCC_APB1ENR1`, holding `RTCAPBEN` (and `PWREN` on the L4).
    pub rcc_apb1enr1: u32,
    /// `RTCAPBEN` mask: enables backup-register reads.
    pub rtcapben: u32,
    /// `PWREN` mask in [`rcc_apb1enr1`](Self::rcc_apb1enr1), or 0.
    pub pwren: u32,
    /// `RTC_TR` (time).
    pub rtc_tr: u32,
    /// `RTC_DR` (date).
    pub rtc_dr: u32,
    /// First backup register.
    pub backup_regs: u32,
    /// Number of 32-bit backup registers.
    pub backup_count: u32,
    /// ECC registers, read before and after the flash: evidence of the
    /// tag's own last ECC fault, which a flash read can overwrite.
    pub ecc_regs: &'static [u32],
    /// Raw register blocks saved verbatim, for evidence beyond the named
    /// values. The block named `"rcc"` is read before any clock enable is
    /// changed.
    pub register_blocks: &'static [McuRegion],
    /// Information-block regions (OTP, memory-mapped option bytes).
    pub info_regions: &'static [McuRegion],
    /// SRAM regions, in capture order. The first is where
    /// [`SramCall`](super::SramCall) downloads images.
    pub sram: &'static [McuRegion],
    /// Offset from [`flash_base`](Self::flash_base) of the tag identity
    /// record: the end of the interrupt vector table
    /// (`embedded/tags/common/core/inc/tag_identity.h`).
    pub identity_offset: u32,
}

impl McuMap {
    /// True for the STM32L432, whose option-byte and reset-flag layouts the
    /// capture manifest decodes in full.
    pub fn is_l4(&self) -> bool {
        self.dev_id == 0x435
    }
}

/// STM32L432 (RM0394 Rev 5, `stm32l432xx.h`).
pub static STM32L432: McuMap = McuMap {
    name: "STM32L432",
    dev_id: 0x435,
    dbgmcu_idcode: 0xE004_2000,
    dbgmcu_apb1fzr1: 0xE004_2008,
    iwdg_freeze: 1 << 12,
    flash_base: 0x0800_0000,
    flash_page: 2048,
    flashsize_reg: 0x1FFF_75E0,
    uid: 0x1FFF_7590,
    flash_optr: 0x4002_2020,
    rcc_csr: 0x4002_1094,
    rcc_bdcr: 0x4002_1090,
    rcc_apb1enr1: 0x4002_1058,
    rtcapben: 1 << 10,
    pwren: 1 << 28,
    rtc_tr: 0x4000_2800,
    rtc_dr: 0x4000_2804,
    backup_regs: 0x4000_2850,
    backup_count: 32,
    ecc_regs: &[0x4002_2018], // FLASH_ECCR
    register_blocks: &[
        McuRegion { name: "rcc", addr: 0x4002_1000, size: 0xA0 },
        McuRegion { name: "flash_regs", addr: 0x4002_2000, size: 0x50 },
        McuRegion { name: "pwr", addr: 0x4000_7000, size: 0x68 },
        // Includes the backup registers.
        McuRegion { name: "rtc", addr: 0x4000_2800, size: 0xD0 },
        McuRegion { name: "dbgmcu", addr: 0xE004_2000, size: 0x10 },
    ],
    info_regions: &[
        McuRegion { name: "otp", addr: 0x1FFF_7000, size: 0x400 },
        McuRegion { name: "option_bytes", addr: 0x1FFF_7800, size: 0x10 },
    ],
    sram: &[
        McuRegion { name: "sram1", addr: 0x2000_0000, size: 48 * 1024 },
        McuRegion { name: "sram2", addr: 0x1000_0000, size: 16 * 1024 },
    ],
    identity_offset: 0x1A0, // after 104 vectors
};

/// STM32U375 (RM0487 Rev 3, `stm32u375xx.h`). Nonsecure aliases: tags run
/// with TrustZone off.
pub static STM32U375: McuMap = McuMap {
    name: "STM32U375",
    dev_id: 0x454,
    dbgmcu_idcode: 0xE004_4000,
    dbgmcu_apb1fzr1: 0xE004_4008,
    iwdg_freeze: 1 << 12,
    flash_base: 0x0800_0000,
    flash_page: 4096,
    flashsize_reg: 0x0BFA_07A0,
    uid: 0x0BFA_0700,
    flash_optr: 0x4002_2040,
    rcc_csr: 0x4003_0D14,
    rcc_bdcr: 0x4003_0D10,
    rcc_apb1enr1: 0x4003_0C9C,
    rtcapben: 1 << 30,
    pwren: 0, // PWR is on AHB here; no PWREN needed
    rtc_tr: 0x4000_7800,
    rtc_dr: 0x4000_7804,
    backup_regs: 0x4000_7D00, // TAMP_BKP0R
    backup_count: 32,
    ecc_regs: &[0x4002_2030, 0x4002_2034], // FLASH_ECCCR, FLASH_ECCDR
    register_blocks: &[
        McuRegion { name: "rcc", addr: 0x4003_0C00, size: 0x118 },
        McuRegion { name: "flash_regs", addr: 0x4002_2000, size: 0x44 },
        McuRegion { name: "rtc", addr: 0x4000_7800, size: 0x80 },
        // Includes the backup registers.
        McuRegion { name: "tamp", addr: 0x4000_7C00, size: 0x180 },
        McuRegion { name: "dbgmcu", addr: 0xE004_4000, size: 0x10 },
    ],
    // Option bytes are not memory-mapped on the U3; FLASH_OPTR only.
    info_regions: &[McuRegion { name: "otp", addr: 0x0BFA_0000, size: 0x200 }],
    sram: &[
        McuRegion { name: "sram1", addr: 0x2000_0000, size: 192 * 1024 },
        McuRegion { name: "sram2", addr: 0x2003_0000, size: 64 * 1024 },
    ],
    identity_offset: 0x240, // after 144 vectors
};

/// All supported maps, in the order [`Session::open`](super::Session::open)
/// probes their `DBGMCU_IDCODE` addresses.
pub static ALL_MCUS: [&McuMap; 2] = [&STM32L432, &STM32U375];

/// The map whose `DEV_ID` matches the low 12 bits of a raw `DBGMCU_IDCODE`,
/// or `None` for an unsupported part.
pub fn find_mcu(idcode: u32) -> Option<&'static McuMap> {
    ALL_MCUS
        .iter()
        .copied()
        .find(|m| (idcode & 0xFFF) == u32::from(m.dev_id))
}

/// Unit tests of the table lookup.
#[cfg(test)]
mod tests {
    use super::*;

    /// Each part's own IDCODE finds its map; the revision bits are ignored.
    #[test]
    fn finds_by_dev_id() {
        assert_eq!(find_mcu(0x1000_6435).unwrap().name, "STM32L432");
        assert_eq!(find_mcu(0x2000_0454).unwrap().name, "STM32U375");
        assert!(find_mcu(0x0000_0415).is_none());
    }

    /// Every captured region is a whole number of words, as the session's
    /// reads require.
    #[test]
    fn regions_are_word_sized() {
        for m in ALL_MCUS {
            for r in m.register_blocks.iter().chain(m.info_regions).chain(m.sram) {
                assert_eq!(r.size % 4, 0, "{} {}", m.name, r.name);
                assert_eq!(r.addr % 4, 0, "{} {}", m.name, r.name);
            }
        }
    }
}
