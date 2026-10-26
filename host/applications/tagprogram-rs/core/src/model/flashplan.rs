//! Which internal flash programming an image erases.
//!
//! Port of qtprogram's `flashplan.cpp`. The image's own linker symbols say
//! what its flash holds:
//!
//! - `__tag_flash_page_size__` 4096: STM32U375. Every saved region is pinned
//!   and kept, so nothing is erased beyond what programming overwrites.
//! - 2048 with a calibration page (`__calibration_start__` above the flash
//!   base): STM32L432 tag; erase the pages below the calibration page only.
//! - 2048 without one: mass erase.
//! - No page-size symbol: an image built before images recorded their layout;
//!   the flash size (`__flash0_end__ - __flash0_base__`) names the MCU and the
//!   plan is a mass erase.

use serde::Serialize;
use std::path::Path;

use super::elf::plan_symbols;

const FLASH_BASE: u32 = 0x0800_0000;
const L432_PAGE: u32 = 2048;
const U375_PAGE: u32 = 4096;

/// What programming erases.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Default)]
pub enum Erase {
    /// No plan: the image could not be read or its layout is unknown.
    #[default]
    Invalid,
    /// Program only; nothing is erased beyond the image.
    None,
    /// Erase the pages below the calibration page, then program.
    BelowCalibration,
    /// Mass erase, then program.
    All,
}

/// The erase plan for one image.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct FlashPlan {
    /// What is erased.
    pub erase: Erase,
    /// Why there is no plan, when `erase` is [`Erase::Invalid`].
    pub error: String,
    /// The image predates the layout symbols; the MCU was inferred from its
    /// flash size.
    pub no_layout: bool,
    /// Flash page size in bytes; 0 when unknown.
    pub page_size: u32,
    /// The last page [`Erase::BelowCalibration`] erases; -1 otherwise.
    pub last_erased_page: i32,
}

impl Default for FlashPlan {
    fn default() -> Self {
        FlashPlan {
            erase: Erase::Invalid,
            error: String::new(),
            no_layout: false,
            page_size: 0,
            last_erased_page: -1,
        }
    }
}

impl FlashPlan {
    fn invalid(error: String) -> Self {
        FlashPlan {
            error,
            ..Default::default()
        }
    }

    /// One line describing the plan, as the Program dialog shows it.
    ///
    /// For an invalid plan, the error.
    pub fn summary(&self) -> String {
        match self.erase {
            Erase::None => "Program only (STM32U375: every saved region is pinned and kept)".into(),
            Erase::BelowCalibration => format!(
                "Erase flash pages 0-{} and program, keeping the calibration page",
                self.last_erased_page
            ),
            Erase::All if self.no_layout => format!(
                "Mass erase and program ({} image built before images recorded their flash layout)",
                if self.page_size == U375_PAGE {
                    "STM32U375"
                } else {
                    "STM32L432"
                }
            ),
            Erase::All => "Mass erase and program (STM32L432 with no calibration page)".into(),
            Erase::Invalid => self.error.clone(),
        }
    }

    /// STM32CubeProgrammer erase arguments for the plan (qtprogram's
    /// `eraseArgs`); empty when nothing is erased. Informational only: this
    /// port programs through probe-rs.
    pub fn erase_args(&self) -> Vec<String> {
        match self.erase {
            Erase::BelowCalibration => vec![
                "-e".into(),
                "[0".into(),
                format!("{}]", self.last_erased_page),
            ],
            Erase::All => vec!["-e".into(), "all".into()],
            _ => Vec::new(),
        }
    }

    /// The tag's calibration survives programming.
    pub fn keeps_calibration(&self) -> bool {
        matches!(self.erase, Erase::None | Erase::BelowCalibration)
    }

    /// The tag's stored configuration survives programming.
    pub fn keeps_configuration(&self) -> bool {
        self.erase == Erase::None
    }

    /// The plan's name in the record's `erase` column: `"none"`,
    /// `"below-calibration"` or `"all"` (empty for an invalid plan).
    pub fn record_label(&self) -> &'static str {
        match self.erase {
            Erase::None => "none",
            Erase::BelowCalibration => "below-calibration",
            Erase::All => "all",
            Erase::Invalid => "",
        }
    }
}

/// Plans the erase for the ELF at `path`.
///
/// Never fails outright: an unreadable file, a non-ELF or an unknown layout
/// gives an [`Erase::Invalid`] plan whose `error` says so (qtprogram's
/// messages).
pub fn plan_flash(path: &Path) -> FlashPlan {
    let p = path.display();
    let Ok(data) = std::fs::read(path) else {
        return FlashPlan::invalid(format!("Cannot read {p}"));
    };
    let symbols = match plan_symbols(
        &data,
        &[
            "__tag_flash_page_size__",
            "__calibration_start__",
            "__calibration_end__",
            "__flash0_base__",
            "__flash0_end__",
        ],
    ) {
        Ok(s) => s,
        Err(e) => {
            return FlashPlan::invalid(format!(
                "{p} is {e}; qtprogram needs the firmware's .elf, whose symbols say what to erase."
            ));
        }
    };
    let get = |name: &str| symbols.get(name).copied();

    let Some(page) = get("__tag_flash_page_size__") else {
        let size = get("__flash0_end__")
            .unwrap_or(0)
            .wrapping_sub(get("__flash0_base__").unwrap_or(0));
        let page_size = match size {
            0x4_0000 => L432_PAGE,
            0x10_0000 => U375_PAGE,
            _ => {
                return FlashPlan::invalid(format!(
                    "{p} does not say which flash layout it was built for \
                     (no __tag_flash_page_size__), nor for which MCU; rebuild it from a current tree."
                ));
            }
        };
        return FlashPlan {
            erase: Erase::All,
            no_layout: true,
            page_size,
            ..Default::default()
        };
    };

    match page {
        U375_PAGE => FlashPlan {
            erase: Erase::None,
            page_size: page,
            ..Default::default()
        },
        L432_PAGE => {
            let start = get("__calibration_start__").unwrap_or(0);
            let end = get("__calibration_end__").unwrap_or(start);
            if start > FLASH_BASE && end > start {
                FlashPlan {
                    erase: Erase::BelowCalibration,
                    page_size: page,
                    last_erased_page: ((start - FLASH_BASE) / L432_PAGE) as i32 - 1,
                    ..Default::default()
                }
            } else {
                FlashPlan {
                    erase: Erase::All,
                    page_size: page,
                    ..Default::default()
                }
            }
        }
        _ => FlashPlan::invalid(format!(
            "{p} was linked for {page}-byte flash pages, which no tag in this tree uses."
        )),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::elf::testelf;

    fn plan(symbols: &[(&str, u32)]) -> FlashPlan {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("t.elf");
        std::fs::write(&path, testelf::build(&[], symbols)).unwrap();
        plan_flash(&path)
    }

    #[test]
    fn u375_programs_only() {
        let p = plan(&[("__tag_flash_page_size__", 4096)]);
        assert_eq!(p.erase, Erase::None);
        assert!(p.keeps_calibration() && p.keeps_configuration());
        assert_eq!(p.record_label(), "none");
        assert!(p.erase_args().is_empty());
    }

    #[test]
    fn l432_with_calibration_keeps_it() {
        let p = plan(&[
            ("__tag_flash_page_size__", 2048),
            ("__calibration_start__", 0x0803_F800),
            ("__calibration_end__", 0x0804_0000),
        ]);
        assert_eq!(p.erase, Erase::BelowCalibration);
        assert_eq!(p.last_erased_page, 126);
        assert_eq!(
            p.summary(),
            "Erase flash pages 0-126 and program, keeping the calibration page"
        );
        assert_eq!(p.erase_args(), ["-e", "[0", "126]"]);
        assert!(p.keeps_calibration() && !p.keeps_configuration());
        assert_eq!(p.record_label(), "below-calibration");
    }

    #[test]
    fn l432_without_calibration_mass_erases() {
        let p = plan(&[("__tag_flash_page_size__", 2048)]);
        assert_eq!(p.erase, Erase::All);
        assert_eq!(
            p.summary(),
            "Mass erase and program (STM32L432 with no calibration page)"
        );
        assert!(!p.keeps_calibration());
    }

    #[test]
    fn no_layout_infers_mcu_from_flash_size() {
        let p = plan(&[
            ("__flash0_base__", 0x0800_0000),
            ("__flash0_end__", 0x0810_0000),
        ]);
        assert_eq!(
            (p.erase, p.no_layout, p.page_size),
            (Erase::All, true, 4096)
        );
        assert!(
            p.summary()
                .starts_with("Mass erase and program (STM32U375 image")
        );
        let p = plan(&[
            ("__flash0_base__", 0x0800_0000),
            ("__flash0_end__", 0x0802_0000),
        ]);
        assert_eq!(p.erase, Erase::Invalid);
        assert!(p.error.contains("no __tag_flash_page_size__"));
    }

    #[test]
    fn odd_page_size_and_non_elf_are_invalid() {
        let p = plan(&[("__tag_flash_page_size__", 1024)]);
        assert!(
            p.error
                .ends_with("was linked for 1024-byte flash pages, which no tag in this tree uses.")
        );
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("x.bin");
        std::fs::write(&path, b"hello").unwrap();
        let p = plan_flash(&path);
        assert_eq!(
            p.error,
            format!(
                "{} is not a 32-bit little-endian ELF file; qtprogram needs the firmware's .elf, \
                 whose symbols say what to erase.",
                path.display()
            )
        );
        let p = plan_flash(&dir.path().join("missing.elf"));
        assert!(p.error.starts_with("Cannot read "));
    }
}
