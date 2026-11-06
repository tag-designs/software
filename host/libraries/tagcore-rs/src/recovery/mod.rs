//! SWD capture and recovery: reach a tag without booting its firmware.
//!
//! Port of the C++ `tagcore/recovery` library onto [`LinkAdapt`], the same
//! link the monitor uses. The design is
//! `host/libraries/tagcore/design/swd-recovery.md`; why the port runs on
//! `LinkAdapt` rather than probe-rs is in
//! `host/libraries/tagcore-rs/design/proposals/swd-recovery-in-rust.md`.
//!
//! The layers, lowest first:
//!
//! | Module | Responsibility |
//! | --- | --- |
//! | [`mcu`] | One table per MCU: flash, SRAM, register addresses, identity-record offset |
//! | [`session`] | Attach without booting, declared exit, core control, reads that continue past faults |
//! | [`image`] | The loadable segments and symbols of a loader or probe ELF |
//! | [`sramcall`] | Download an image to SRAM1 and call a symbol with the ST loader convention |
//! | [`xflash`] | A loader's `Serve()` session: probe, read, read page, erase, program |
//! | [`identity`] | Parse the identity record from captured internal flash |
//! | [`external`], [`capture`] | The capture procedure: ordering, output directory, manifest |
//! | [`build`] | The firmware build stored with a capture, and whether it matches the flash |
//!
//! A recovery session and a monitor session are alternatives: each claims
//! the base for its lifetime. Procedures that need both run them in
//! sequence.
//!
//! [`LinkAdapt`]: crate::LinkAdapt

pub mod build;
pub mod capture;
pub mod external;
pub mod identity;
pub mod image;
pub mod mcu;
pub mod session;
pub mod sramcall;
pub mod xflash;

pub use build::{BuildFiles, BuildRecord, ImageCheck};
pub use capture::{CaptureOptions, CaptureResult, capture_state, capture_tag, find_loader};
pub use external::{ExternalCaptureOptions, ExternalCaptureResult, capture_external_flash};
pub use identity::{IdentityRecord, find_identity_record, parse_identity_record, record_offsets};
pub use image::TargetImage;
pub use mcu::{McuMap, McuRegion, find_mcu};
pub use session::{AddressRange, AttachInfo, Session, SwdExit};
pub use sramcall::SramCall;
pub use xflash::ExternalFlash;
